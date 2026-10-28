#include "app.h"

#include "text_util.h"

#include <wincodec.h>

#include <algorithm>
#include <vector>

#pragma comment(lib, "windowscodecs.lib")

namespace {

// A 32bpp top-down DIB plus the memory DC used to draw into it.
struct Surface {
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previous = nullptr;
    void* bits = nullptr;
    int width = 0;
    int height = 0;

    ~Surface() {
        if (dc) {
            if (previous) SelectObject(dc, previous);
            DeleteDC(dc);
        }
        if (bitmap) DeleteObject(bitmap);
    }
};

bool createSurface(int size, const qr::Symbol& symbol, Surface& surface) {
    size = std::max(64, size);

    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = size;
    info.bmiHeader.biHeight = -size;  // top-down so the rows match the symbol
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    HDC screen = GetDC(nullptr);
    surface.bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &surface.bits, nullptr, 0);
    surface.dc = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);
    if (!surface.bitmap || !surface.dc || !surface.bits) return false;

    surface.previous = SelectObject(surface.dc, surface.bitmap);
    RECT all = {0, 0, size, size};
    HBRUSH white = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(surface.dc, &all, white);
    DeleteObject(white);

    drawQrSymbol(surface.dc, all, symbol, 0);
    surface.width = size;
    surface.height = size;
    return true;
}

// PNG through WIC: no GDI+, no third-party encoder.
bool writePng(const Surface& surface, const std::wstring& path) {
    IWICImagingFactory* factory = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory))))
        return false;

    IWICStream* stream = nullptr;
    IWICBitmapEncoder* encoder = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    IPropertyBag2* options = nullptr;
    bool written = false;

    if (SUCCEEDED(factory->CreateStream(&stream)) &&
        SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) &&
        SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
        SUCCEEDED(encoder->Initialize(stream, WICBitmapEncoderNoCache)) &&
        SUCCEEDED(encoder->CreateNewFrame(&frame, &options)) &&
        SUCCEEDED(frame->Initialize(options)) &&
        SUCCEEDED(frame->SetSize((UINT)surface.width, (UINT)surface.height))) {
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
        const UINT stride = (UINT)surface.width * 4;
        if (SUCCEEDED(frame->SetPixelFormat(&format)) &&
            SUCCEEDED(frame->WritePixels((UINT)surface.height, stride,
                                         stride * (UINT)surface.height,
                                         static_cast<BYTE*>(surface.bits))) &&
            SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit()))
            written = true;
    }

    if (options) options->Release();
    if (frame) frame->Release();
    if (encoder) encoder->Release();
    if (stream) stream->Release();
    factory->Release();
    return written;
}

}  // namespace

void drawQrSymbol(HDC hdc, const RECT& area, const qr::Symbol& symbol, int padding) {
    const int width = area.right - area.left;
    const int height = area.bottom - area.top;
    if (width <= 0 || height <= 0 || symbol.size <= 0) return;

    int side = std::min(width, height) - padding;
    if (side < 8) side = std::min(width, height);
    const int modules = symbol.size + 8;  // 4 module quiet zone on each side
    int module = side / modules;
    if (module < 1) module = 1;
    const int drawn = module * modules;
    const int left = area.left + (width - drawn) / 2;
    const int top = area.top + (height - drawn) / 2;

    RECT background = {left, top, left + drawn, top + drawn};
    HBRUSH white = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(hdc, &background, white);
    DeleteObject(white);

    HBRUSH dark = CreateSolidBrush(RGB(0, 0, 0));
    for (int y = 0; y < symbol.size; ++y) {
        int runStart = -1;
        for (int x = 0; x <= symbol.size; ++x) {
            const bool isDark = x < symbol.size && symbol.get(x, y);
            if (isDark && runStart < 0) {
                runStart = x;
            } else if (!isDark && runStart >= 0) {
                RECT run = {left + (runStart + 4) * module, top + (y + 4) * module,
                            left + (x + 4) * module, top + (y + 5) * module};
                FillRect(hdc, &run, dark);
                runStart = -1;
            }
        }
    }
    DeleteObject(dark);
}

bool saveQrPng(const qr::Symbol& symbol, int pixelSize, const std::wstring& path) {
    Surface surface;
    if (!createSurface(pixelSize, symbol, surface)) return false;
    return writePng(surface, path);
}

bool copyQrToClipboard(HWND owner, const qr::Symbol& symbol, int pixelSize) {
    Surface surface;
    if (!createSurface(pixelSize, symbol, surface)) return false;

    const int width = surface.width;
    const int height = surface.height;
    const size_t stride = (size_t)width * 4;

    // CF_DIB: header plus bottom-up pixels, 32bpp.
    BITMAPINFOHEADER header = {};
    header.biSize = sizeof(BITMAPINFOHEADER);
    header.biWidth = width;
    header.biHeight = height;
    header.biPlanes = 1;
    header.biBitCount = 32;
    header.biCompression = BI_RGB;
    const size_t pixels = stride * (size_t)height;

    HGLOBAL dib = GlobalAlloc(GMEM_MOVEABLE, sizeof(header) + pixels);
    bool copied = false;

    if (dib) {
        BYTE* target = static_cast<BYTE*>(GlobalLock(dib));
        if (target) {
            memcpy(target, &header, sizeof(header));
            const BYTE* source = static_cast<const BYTE*>(surface.bits);
            for (int y = 0; y < height; ++y) {
                memcpy(target + sizeof(header) + (size_t)y * stride,
                       source + (size_t)(height - 1 - y) * stride, stride);
            }
            GlobalUnlock(dib);
        }
    }

    // CF_BITMAP for older consumers.
    HDC screen = GetDC(nullptr);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, width, height);
    HDC memory = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);
    if (bitmap && memory) {
        HGDIOBJ old = SelectObject(memory, bitmap);
        BitBlt(memory, 0, 0, width, height, surface.dc, 0, 0, SRCCOPY);
        SelectObject(memory, old);
    }

    if (OpenClipboard(owner)) {
        EmptyClipboard();
        if (dib && SetClipboardData(CF_DIB, dib)) {
            dib = nullptr;  // owned by the clipboard now
            copied = true;
        }
        if (bitmap && SetClipboardData(CF_BITMAP, bitmap)) bitmap = nullptr;
        CloseClipboard();
    }

    if (dib) GlobalFree(dib);
    if (bitmap) DeleteObject(bitmap);
    if (memory) DeleteDC(memory);
    return copied;
}

std::wstring defaultFileNameFor(const std::wstring& content) {
    std::wstring name;
    for (wchar_t c : content) {
        if (c == L'\r' || c == L'\n' || c == L'\t') {
            if (!name.empty() && name.back() != L'-') name.push_back(L'-');
            continue;
        }
        if (name.size() >= 40) break;
        name.push_back(tu::containsChar(L"\\/:*?\"<>|", c) ? L'-' : c);
    }
    while (!name.empty() && (name.back() == L'-' || name.back() == L'.' || name.back() == ' '))
        name.pop_back();
    if (name.empty()) name = L"qrcode";
    return name + L".png";
}
