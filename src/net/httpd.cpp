#include "httpd.h"

#include <ws2tcpip.h>

#include <wincodec.h>

#include <algorithm>
#include <new>
#include <vector>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "windowscodecs.lib")

// ------------------------------------------------------------------ tuning

namespace {

constexpr int kMaxCache = 4;                // tokens kept at once (newest first)
constexpr uint64_t kTtlMs = 5 * 60 * 1000;  // a link stops working after five minutes
constexpr size_t kMaxReceive = 4096;        // a request head is a few hundred bytes
constexpr int kMaxConnections = 8;
constexpr DWORD kSocketTimeoutMs = 4000;

const char kHeadOk[] =
    "HTTP/1.1 200 OK\r\nConnection: close\r\nCache-Control: no-store\r\n"
    "X-Content-Type-Options: nosniff\r\n";
const char kHeadNotFound[] =
    "HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Type: text/plain; charset=utf-8\r\n"
    "Content-Length: 9\r\n\r\nNot found";
const char kHeadNotFoundForBody[] =
    "HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Type: text/plain; charset=utf-8\r\n"
    "Content-Length: 9\r\n\r\n";
const char kHeadBadRequest[] =
    "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";

// ------------------------------------------------------------------ state

struct CacheEntry {
    char token[17];
    char extension[8];  // ".png" or "": the extension is part of the URL
    uint64_t born;
    std::vector<uint8_t> bytes;
};

SOCKET g_listen = INVALID_SOCKET;
HWND g_window = nullptr;
std::wstring g_base;
CacheEntry g_cache[kMaxCache];
int g_cacheCount = 0;
SOCKET g_connections[kMaxConnections];
int g_connectionCount = 0;
bool g_wsaReady = false;
uint32_t g_randomState = 0;

// ---------------------------------------------------------------- connections

bool isTracked(SOCKET socket) {
    for (int i = 0; i < g_connectionCount; ++i) {
        if (g_connections[i] == socket) return true;
    }
    return false;
}

void trackConnection(SOCKET socket) {
    if (isTracked(socket)) return;
    if (g_connectionCount < kMaxConnections) {
        g_connections[g_connectionCount++] = socket;
    } else {
        closesocket(socket);
    }
}

void closeConnection(SOCKET socket) {
    for (int i = 0; i < g_connectionCount; ++i) {
        if (g_connections[i] == socket) {
            g_connections[i] = g_connections[g_connectionCount - 1];
            --g_connectionCount;
            break;
        }
    }
    closesocket(socket);
}

// 192.168/16, 10/8, 172.16/12 and loopback only: the server stays unreachable from the
// internet even if the router forwards the port.
bool peerIsPrivate(const sockaddr_in& peer) {
    const uint32_t host = ntohl(peer.sin_addr.S_un.S_addr);
    if ((host >> 24) == 10) return true;
    if ((host >> 20) == 0xAC1u) return true;  // 172.16.0.0/12
    if ((host >> 16) == 0xC0A8u) return true; // 192.168.0.0/16
    return (host >> 24) == 127;               // loopback: testing from this machine
}

// ---------------------------------------------------------------- payload store

void clearEntry(CacheEntry& entry) {
    for (int i = 0; i < 17; ++i) entry.token[i] = 0;
    for (int i = 0; i < 8; ++i) entry.extension[i] = 0;
    entry.born = 0;
    entry.bytes.clear();
}

void dropEntry(int index) {
    for (int i = index; i + 1 < g_cacheCount; ++i) g_cache[i] = std::move(g_cache[i + 1]);
    --g_cacheCount;
    clearEntry(g_cache[g_cacheCount]);
}

void expireEntries() {
    const uint64_t now = GetTickCount64();
    for (int i = 0; i < g_cacheCount;) {
        if (now - g_cache[i].born > kTtlMs)
            dropEntry(i);
        else
            ++i;
    }
}

bool sameText(const char* a, const char* b) {
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == 0 && *b == 0;
}

// One entry serves both URLs: /s/<token> renders the page, /s/<token>.png streams the
// bytes, so the lookup only matches on the token.
CacheEntry* findEntry(const char* token) {
    expireEntries();
    for (int i = 0; i < g_cacheCount; ++i) {
        if (sameText(g_cache[i].token, token)) return &g_cache[i];
    }
    return nullptr;
}

// ---------------------------------------------------------------- routing

// No member initialisers anywhere in this file: they make the compiler emit an
// initialiser copy through __chkstk, which the CRT-less link cannot resolve.
struct Target {
    char token[17];
    char extension[8];
};

size_t appendText(char* buffer, size_t capacity, size_t used, const char* text) {
    while (*text && used + 1 < capacity) buffer[used++] = *text++;
    buffer[used] = 0;
    return used;
}

size_t appendNumber(char* buffer, size_t capacity, size_t used, unsigned long value) {
    char digits[24];
    int count = 0;
    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value && count < (int)sizeof(digits));
    while (count > 0 && used + 1 < capacity) buffer[used++] = digits[--count];
    buffer[used] = 0;
    return used;
}

// Only "/s/<token><ext>" is served, and only for tokens this process published, so
// there is no file name to sanitise and nothing on disk to leak.
void parseTarget(const char* path, int length, Target& target) {
    target.token[0] = 0;
    target.extension[0] = 0;
    if (length < 5 || path[0] != '/' || path[1] != 's' || path[2] != '/') return;
    int end = 3;
    while (end < length && path[end] != '.') ++end;
    const int tokenLength = end - 3;
    if (tokenLength <= 0 || tokenLength > 16) return;
    for (int i = 0; i < tokenLength; ++i) target.token[i] = path[3 + i];
    target.token[tokenLength] = 0;
    if (end < length) {
        const int extensionLength = length - end;
        if (extensionLength >= (int)sizeof(target.extension)) {
            target.token[0] = 0;
            return;
        }
        for (int i = 0; i < extensionLength; ++i) target.extension[i] = path[end + i];
        target.extension[extensionLength] = 0;
    }
}

// ---------------------------------------------------------------- the page

const char kPageHead[] =
    "<!doctype html><html lang=\"zh\"><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1,viewport-fit=cover\">"
    "<title>PtFastQRcode</title><style>"
    "html,body{margin:0;height:100%;background:#1c1c1c;color:#9a9a9a;"
    "font:13px/1.5 -apple-system,'Segoe UI',Roboto,sans-serif;-webkit-text-size-adjust:100%}"
    "body{display:flex;flex-direction:column}"
    "main{flex:1;display:flex;align-items:center;justify-content:center;padding:16px;"
    "box-sizing:border-box;min-height:0}"
    "img{max-width:100%;max-height:100%;object-fit:contain;border-radius:6px;background:#fff;"
    "box-shadow:0 8px 32px rgba(0,0,0,.55)}"
    "footer{padding:0 16px 16px;text-align:center;font-size:12px;color:#7a7a7a}"
    "</style></head><body><main><img src=\"";

const char kPageTail[] =
    "\" alt=\"图片\"></main><footer>PtFastQRcode &middot; 5 分钟内有效</footer>"
    "</body></html>";

bool sendAll(SOCKET socket, const char* data, size_t length) {
    while (length > 0) {
        const int chunk = (int)std::min<size_t>(length, 0x40000000);
        const int sent = send(socket, data, chunk, 0);
        if (sent <= 0) return false;
        data += sent;
        length -= (size_t)sent;
    }
    return true;
}

void sendHead(SOCKET socket, const char* head, const char* contentType, size_t bodyLength) {
    char header[512];
    size_t used = appendText(header, sizeof(header), 0, head);
    if (contentType) {
        used = appendText(header, sizeof(header), used, "Content-Type: ");
        used = appendText(header, sizeof(header), used, contentType);
        used = appendText(header, sizeof(header), used, "\r\n");
    }
    used = appendText(header, sizeof(header), used, "Content-Length: ");
    used = appendNumber(header, sizeof(header), used, (unsigned long)bodyLength);
    used = appendText(header, sizeof(header), used, "\r\n\r\n");
    sendAll(socket, header, used);
}

// No runtime: the compiler may emit a stack probe (__chkstk) for a large local buffer, and
// that symbol only exists in the CRT. Keeping request heads on the heap avoids it.
inline char* scratch() {
    static char* buffer = nullptr;
    if (!buffer) buffer = (char*)HeapAlloc(GetProcessHeap(), 0, kMaxReceive + 1);
    return buffer;
}

void handleConnection(SOCKET socket) {
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&kSocketTimeoutMs,
               sizeof(kSocketTimeoutMs));
    setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, (const char*)&kSocketTimeoutMs,
               sizeof(kSocketTimeoutMs));

    char* request = scratch();
    if (!request) {
        closeConnection(socket);
        return;
    }
    int received = 0;
    // Head only, one shot: everything after the request line is ignored, so the
    // receive buffer stays small and no request body can be buffered.
    for (int attempt = 0; attempt < 4 && received < 3; ++attempt) {
        if (attempt > 0) {
            fd_set readable;
            FD_ZERO(&readable);
            FD_SET(socket, &readable);
            struct timeval wait = {2, 0};
            if (select(0, &readable, nullptr, nullptr, &wait) <= 0) break;
        }
        received = recv(socket, request, (int)kMaxReceive, 0);
        if (received <= 0) break;
    }
    if (received <= 0) {
        closeConnection(socket);
        return;
    }
    request[received] = 0;

    // "GET /s/<token>.png HTTP/1.1"
    if (received < 5 || request[0] != 'G' || request[1] != 'E' || request[2] != 'T' ||
        request[3] != ' ') {
        sendAll(socket, kHeadBadRequest, sizeof(kHeadBadRequest) - 1);
        closeConnection(socket);
        return;
    }

    int pathStart = 4;
    int pathEnd = pathStart;
    while (pathEnd < received && request[pathEnd] != ' ' && request[pathEnd] != '?' &&
           request[pathEnd] != '\r' && request[pathEnd] != '\n')
        ++pathEnd;

    Target target;
    parseTarget(request + pathStart, pathEnd - pathStart, target);

    CacheEntry* entry = findEntry(target.token);
    const bool wantsBytes = target.extension[0] != 0;
    if (!entry || (wantsBytes && (!sameText(target.extension, ".png") || !entry->extension[0]))) {
        sendAll(socket, kHeadNotFoundForBody, sizeof(kHeadNotFoundForBody) - 1);
        sendAll(socket, "Not found", 9);
        closeConnection(socket);
        return;
    }

    if (target.extension[0]) {
        sendHead(socket, kHeadOk, "image/png", entry->bytes.size());
        sendAll(socket, (const char*)entry->bytes.data(), entry->bytes.size());
    } else {
        // The page embeds the byte URL, extension included: "/s/<token>" alone would make
        // the browser ask for the HTML again and show a broken image.
        char opening[40] = "/s/";
        size_t openingLength = 3;
        for (int i = 0; entry->token[i] && openingLength + 1 < sizeof(opening); ++i)
            opening[openingLength++] = entry->token[i];
        for (int i = 0; entry->extension[i] && openingLength + 1 < sizeof(opening); ++i)
            opening[openingLength++] = entry->extension[i];
        opening[openingLength] = 0;

        const size_t headLength = sizeof(kPageHead) - 1;
        const size_t tailLength = sizeof(kPageTail) - 1;
        sendHead(socket, kHeadOk, "text/html; charset=utf-8",
                 headLength + openingLength + tailLength);
        sendAll(socket, kPageHead, headLength);
        sendAll(socket, opening, openingLength);
        sendAll(socket, kPageTail, tailLength);
    }
    closeConnection(socket);
}

// ---------------------------------------------------------------- picture -> PNG

// A COM stream over a byte vector: WIC needs a seekable IStream, and encoding into
// memory keeps the picture out of the file system entirely.
class MemoryStream : public IStream {
   public:
    explicit MemoryStream(std::vector<uint8_t>& target) : m_target(target) {}

    STDMETHODIMP QueryInterface(REFIID riid, void** object) override {
        if (!object) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_ISequentialStream || riid == IID_IStream) {
            *object = (IStream*)this;
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_refs); }
    STDMETHODIMP_(ULONG) Release() override {
        const ULONG remaining = InterlockedDecrement(&m_refs);
        if (remaining == 0) delete this;
        return remaining;
    }
    STDMETHODIMP Read(void*, ULONG, ULONG* read) override {
        if (read) *read = 0;
        return E_NOTIMPL;
    }
    STDMETHODIMP Write(const void* data, ULONG count, ULONG* written) override {
        const size_t end = m_position + count;
        if (end > m_target.size()) m_target.resize(end);
        memcpy(m_target.data() + m_position, data, count);
        m_position = end;
        if (written) *written = count;
        return S_OK;
    }
    STDMETHODIMP Seek(LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER* position) override {
        LONGLONG base = 0;
        switch (origin) {
            case STREAM_SEEK_SET: base = 0; break;
            case STREAM_SEEK_CUR: base = (LONGLONG)m_position; break;
            case STREAM_SEEK_END: base = (LONGLONG)m_target.size(); break;
            default: return STG_E_INVALIDFUNCTION;
        }
        const LONGLONG next = base + move.QuadPart;
        if (next < 0) return STG_E_INVALIDFUNCTION;
        m_position = (size_t)next;
        if (position) position->QuadPart = (ULONGLONG)m_position;
        return S_OK;
    }
    STDMETHODIMP SetSize(ULARGE_INTEGER size) override {
        m_target.resize((size_t)size.QuadPart);
        if (m_position > m_target.size()) m_position = m_target.size();
        return S_OK;
    }
    STDMETHODIMP CopyTo(IStream*, ULARGE_INTEGER, ULARGE_INTEGER*, ULARGE_INTEGER*) override {
        return E_NOTIMPL;
    }
    STDMETHODIMP Commit(DWORD) override { return S_OK; }
    STDMETHODIMP Revert() override { return E_NOTIMPL; }
    STDMETHODIMP LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return E_NOTIMPL; }
    STDMETHODIMP UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return E_NOTIMPL; }
    STDMETHODIMP Stat(STATSTG* stats, DWORD) override {
        if (!stats) return E_POINTER;
        for (size_t i = 0; i < sizeof(*stats); ++i) ((BYTE*)stats)[i] = 0;
        stats->cbSize.QuadPart = m_target.size();
        stats->type = STGTY_STREAM;
        stats->grfMode = STGM_WRITE;
        return S_OK;
    }
    STDMETHODIMP Clone(IStream**) override { return E_NOTIMPL; }

   private:
    LONG m_refs = 1;
    std::vector<uint8_t>& m_target;
    size_t m_position = 0;
};

// ---------------------------------------------------------------- bitmap access

// Copies an HBITMAP into a top-down 32bpp DIB, keeping the alpha channel the source
// carries (icon and PNG clipboard payloads do; plain DDBs come out opaque).
bool copyBitmapToDib(HBITMAP bitmap, std::vector<uint8_t>& pixels, int& width, int& height) {
    BITMAP header = {};
    if (!GetObjectW(bitmap, sizeof(header), &header)) return false;
    width = header.bmWidth;
    height = header.bmHeight;
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384) return false;

    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) return false;
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    pixels.assign((size_t)width * (size_t)height * 4, 0);
    HGDIOBJ previous = SelectObject(dc, bitmap);
    const int lines = GetDIBits(dc, bitmap, 0, (UINT)height, pixels.data(), &info, DIB_RGB_COLORS);
    SelectObject(dc, previous);
    DeleteDC(dc);
    if (lines != height) return false;

    // DDBs have no alpha: make what would otherwise be fully transparent black obvious
    // by treating a fully zero alpha plane as opaque.
    bool anyAlpha = false;
    for (size_t i = 3; i < pixels.size(); i += 4) {
        if (pixels[i] != 0) {
            anyAlpha = true;
            break;
        }
    }
    if (!anyAlpha) {
        for (size_t i = 3; i < pixels.size(); i += 4) pixels[i] = 255;
    }
    return true;
}

}  // namespace

namespace net {

// ---------------------------------------------------------------- address lookup

std::wstring localAddress() {
    SOCKET probe = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (probe == INVALID_SOCKET) return std::wstring();

    sockaddr_in destination = {};
    destination.sin_family = AF_INET;
    destination.sin_port = htons(53);
    destination.sin_addr.S_un.S_addr = inet_addr("223.5.5.5");

    std::wstring address;
    // connect() on UDP sends nothing; it only makes the stack pick the outgoing interface,
    // so virtual adapters and VPN tunnels (not on the default route) drop out.
    if (connect(probe, (const sockaddr*)&destination, sizeof(destination)) == 0) {
        sockaddr_in local = {};
        int length = sizeof(local);
        if (getsockname(probe, (sockaddr*)&local, &length) == 0) {
            char text[32] = {};
            if (inet_ntop(AF_INET, &local.sin_addr, text, sizeof(text))) {
                wchar_t wide[32] = {};
                for (int i = 0; i < 31 && text[i]; ++i) wide[i] = (wchar_t)text[i];
                address = wide;
            }
        }
    }
    closesocket(probe);
    return address;
}

// ---------------------------------------------------------------- lifecycle

bool start(HWND window) {
    if (g_listen != INVALID_SOCKET) return true;

    if (!g_wsaReady) {
        WSADATA data = {};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return false;
        g_wsaReady = true;
    }

    const std::wstring address = localAddress();
    if (address.empty()) return false;

    SOCKET listening = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listening == INVALID_SOCKET) return false;

    const BOOL exclusive = TRUE;
    setsockopt(listening, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&exclusive,
               sizeof(exclusive));

    sockaddr_in local = {};
    local.sin_family = AF_INET;
    local.sin_port = 0;  // a free port, chosen by the stack
    local.sin_addr.S_un.S_addr = INADDR_ANY;
    if (bind(listening, (const sockaddr*)&local, sizeof(local)) != 0 ||
        listen(listening, 16) != 0 ||
        WSAAsyncSelect(listening, window, WM_APP_HTTP, FD_ACCEPT | FD_CLOSE) != 0) {
        closesocket(listening);
        return false;
    }

    int length = sizeof(local);
    getsockname(listening, (sockaddr*)&local, &length);
    g_listen = listening;
    g_window = window;
    wchar_t port[8] = {};
    wsprintfW(port, L"%u", (unsigned)ntohs(local.sin_port));
    g_base = L"http://" + address + L":" + port;
    return true;
}

void stop() {
    for (int i = 0; i < g_connectionCount; ++i) closesocket(g_connections[i]);
    g_connectionCount = 0;
    if (g_listen != INVALID_SOCKET) {
        closesocket(g_listen);
        g_listen = INVALID_SOCKET;
    }
    g_base.clear();
    for (int i = 0; i < kMaxCache; ++i) clearEntry(g_cache[i]);
    g_cacheCount = 0;
    // WSACleanup is never called: ws2_32 stays loaded for the life of the process and
    // tearing the stack down would only make a later restart slower.
}

bool running() { return g_listen != INVALID_SOCKET; }

std::wstring baseUrl() { return g_base; }

std::wstring pageUrl(const std::wstring& path) { return g_base + path; }

// ---------------------------------------------------------------- publishing

std::wstring publish(const void* data, std::size_t size, Kind kind, bool& ok) {
    ok = false;
    if (g_listen == INVALID_SOCKET || size == 0) return std::wstring();

    // xorshift32 seeded from the clock and the payload: the token only has to be
    // unguessable to someone scanning the LAN, and every link dies after five minutes.
    if (g_randomState == 0)
        g_randomState = (uint32_t)GetTickCount64() ^ (uint32_t)(uintptr_t)data ^ 0x9E3779B9u;

    CacheEntry entry;
    clearEntry(entry);
    static const char kHex[] = "0123456789abcdef";
    for (int i = 0; i < 16; ++i) {
        g_randomState ^= g_randomState << 13;
        g_randomState ^= g_randomState >> 17;
        g_randomState ^= g_randomState << 5;
        entry.token[i] = kHex[g_randomState & 15];
    }
    if (kind == Kind::Png) {
        entry.extension[0] = '.';
        entry.extension[1] = 'p';
        entry.extension[2] = 'n';
        entry.extension[3] = 'g';
    }
    entry.born = GetTickCount64();
    entry.bytes.assign((const uint8_t*)data, (const uint8_t*)data + size);

    expireEntries();
    if (g_cacheCount == kMaxCache) dropEntry(kMaxCache - 1);
    for (int i = g_cacheCount; i > 0; --i) g_cache[i] = std::move(g_cache[i - 1]);
    g_cache[0] = std::move(entry);
    ++g_cacheCount;

    std::wstring path = L"/s/";
    for (int i = 0; i < 16; ++i) path.push_back((wchar_t)g_cache[0].token[i]);
    for (const char* c = g_cache[0].extension; *c; ++c) path.push_back((wchar_t)*c);
    ok = true;
    return path;
}

// ---------------------------------------------------------------- dispatch

bool handleEvent(WPARAM socketValue, LPARAM event) {
    const SOCKET socket = (SOCKET)socketValue;
    if (g_listen != INVALID_SOCKET && socket == g_listen) {
        if (LOWORD(event) != FD_ACCEPT) return true;
        for (;;) {
            sockaddr_in peer = {};
            int length = sizeof(peer);
            const SOCKET accepted = accept(g_listen, (sockaddr*)&peer, &length);
            if (accepted == INVALID_SOCKET) break;
            if (!peerIsPrivate(peer)) {
                closesocket(accepted);
                continue;
            }
            trackConnection(accepted);
            WSAAsyncSelect(accepted, g_window, WM_APP_HTTP, FD_READ | FD_CLOSE);
        }
        return true;
    }

    if (!isTracked(socket)) return false;  // stale notification from an older session

    const int code = LOWORD(event);
    if (code == FD_CLOSE) {
        closeConnection(socket);
        return true;
    }
    if (code == FD_READ) {
        WSAAsyncSelect(socket, g_window, WM_APP_HTTP, 0);  // one request per connection
        handleConnection(socket);
        return true;
    }
    return true;
}

// ---------------------------------------------------------------- pictures

bool encodeBitmapAsPng(HBITMAP bitmap, std::string& png) {
    png.clear();
    std::vector<uint8_t> pixels;
    int width = 0, height = 0;
    if (!copyBitmapToDib(bitmap, pixels, width, height)) return false;
    return encodePixelsAsPng(pixels.data(), width, height, png);
}

bool encodePixelsAsPng(const void* pixels, int width, int height, std::string& png) {
    png.clear();
    if (!pixels || width <= 0 || height <= 0) return false;
    if (!ensureCom()) return false;

    IWICImagingFactory* factory = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory))))
        return false;

    const UINT stride = (UINT)width * 4;
    IWICBitmap* bitmap = nullptr;
    IWICBitmapEncoder* encoder = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    IPropertyBag2* options = nullptr;
    std::vector<uint8_t> bytes;
    MemoryStream* stream = new MemoryStream(bytes);
    bool done = false;

    if (SUCCEEDED(factory->CreateBitmapFromMemory(
                      (UINT)width, (UINT)height, GUID_WICPixelFormat32bppBGRA, stride,
                      stride * (UINT)height, (BYTE*)pixels, &bitmap)) &&
        SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
        SUCCEEDED(encoder->Initialize(stream, WICBitmapEncoderNoCache)) &&
        SUCCEEDED(encoder->CreateNewFrame(&frame, &options)) &&
        SUCCEEDED(frame->Initialize(options)) &&
        SUCCEEDED(frame->SetSize((UINT)width, (UINT)height))) {
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
        if (SUCCEEDED(frame->SetPixelFormat(&format)) && SUCCEEDED(frame->WriteSource(bitmap, nullptr)) &&
            SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit())) {
            png.assign((const char*)bytes.data(), bytes.size());
            done = !png.empty();
        }
    }

    if (options) options->Release();
    if (frame) frame->Release();
    if (encoder) encoder->Release();
    if (bitmap) bitmap->Release();
    factory->Release();
    stream->Release();
    return done;
}

}  // namespace net
