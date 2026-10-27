#include "app.h"

#include <algorithm>
#include <vector>

namespace {

int getEncoderClsid(const WCHAR* mimeType, CLSID* clsid) {
    UINT count = 0, size = 0;
    if (Gdiplus::GetImageEncodersSize(&count, &size) != Gdiplus::Ok || size == 0) return -1;
    std::vector<BYTE> buffer(size);
    Gdiplus::ImageCodecInfo* codecs = (Gdiplus::ImageCodecInfo*)buffer.data();
    if (Gdiplus::GetImageEncoders(count, size, codecs) != Gdiplus::Ok) return -1;
    for (UINT i = 0; i < count; ++i) {
        if (wcscmp(codecs[i].MimeType, mimeType) == 0) {
            *clsid = codecs[i].Clsid;
            return (int)i;
        }
    }
    return -1;
}

}  // namespace

Gdiplus::Bitmap* renderQrBitmap(const qr::Symbol& symbol, int pixelSize) {
    pixelSize = std::max(64, pixelSize);
    Gdiplus::Bitmap* bitmap = new Gdiplus::Bitmap(pixelSize, pixelSize, PixelFormat32bppRGB);
    if (!bitmap || bitmap->GetLastStatus() != Gdiplus::Ok) {
        delete bitmap;
        return nullptr;
    }

    Gdiplus::Graphics g(bitmap);
    g.Clear(Gdiplus::Color(255, 255, 255, 255));
    g.SetSmoothingMode(Gdiplus::SmoothingModeNone);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

    const int quiet = 4;
    int modules = symbol.size + quiet * 2;
    int moduleSize = pixelSize / modules;
    if (moduleSize < 1) moduleSize = 1;
    int drawn = moduleSize * modules;
    int offset = (pixelSize - drawn) / 2;

    Gdiplus::SolidBrush dark(Gdiplus::Color(255, 0, 0, 0));
    for (int y = 0; y < symbol.size; ++y) {
        int runStart = -1;
        for (int x = 0; x <= symbol.size; ++x) {
            bool darkHere = x < symbol.size && symbol.get(x, y);
            if (darkHere && runStart < 0) {
                runStart = x;
            } else if (!darkHere && runStart >= 0) {
                int left = offset + (runStart + quiet) * moduleSize;
                int width = (x - runStart) * moduleSize;
                g.FillRectangle(&dark, left, offset + (y + quiet) * moduleSize, width, moduleSize);
                runStart = -1;
            }
        }
    }
    return bitmap;
}

void drawQrSymbol(HDC hdc, const RECT& area, const qr::Symbol& symbol) {
    const int width = area.right - area.left;
    const int height = area.bottom - area.top;
    if (width <= 0 || height <= 0 || symbol.size <= 0) return;

    int side = std::min(width, height) - S(28);
    if (side < S(32)) side = std::min(width, height);
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

bool saveBitmapAsPng(Gdiplus::Bitmap* bitmap, const std::wstring& path) {
    if (!bitmap) return false;
    CLSID clsid;
    if (getEncoderClsid(L"image/png", &clsid) < 0) return false;
    ensureDirectory(path.substr(0, path.find_last_of(L'\\')));
    return bitmap->Save(path.c_str(), &clsid, nullptr) == Gdiplus::Ok;
}

bool copyBitmapToClipboard(HWND owner, Gdiplus::Bitmap* bitmap) {
    if (!bitmap) return false;
    int width = (int)bitmap->GetWidth();
    int height = (int)bitmap->GetHeight();

    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* pixels = nullptr;
    HDC screen = GetDC(nullptr);
    HBITMAP dib = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    HDC dibDc = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);
    if (!dib || !pixels || !dibDc) {
        if (dib) DeleteObject(dib);
        if (dibDc) DeleteDC(dibDc);
        return false;
    }

    HGDIOBJ old = SelectObject(dibDc, dib);
    Gdiplus::Graphics g(dibDc);
    g.DrawImage(bitmap, 0, 0, width, height);
    SelectObject(dibDc, old);
    DeleteDC(dibDc);

    // CF_DIB (global memory) keeps alpha-free 32bpp data that every Windows app accepts.
    size_t headerSize = sizeof(BITMAPINFOHEADER);
    size_t pixelSize = (size_t)width * height * 4;
    HGLOBAL global = GlobalAlloc(GMEM_MOVEABLE, headerSize + pixelSize);
    HGLOBAL globalBitmap = nullptr;
    bool ok = false;
    if (global) {
        void* target = GlobalLock(global);
        if (target) {
            memcpy(target, &info.bmiHeader, headerSize);
            memcpy((BYTE*)target + headerSize, pixels, pixelSize);
            GlobalUnlock(global);
        }
    }

    // CF_BITMAP copy for legacy consumers (Paint, older Office).
    HDC screen2 = GetDC(nullptr);
    HBITMAP copy = CreateCompatibleBitmap(screen2, width, height);
    HDC copyDc = CreateCompatibleDC(screen2);
    ReleaseDC(nullptr, screen2);
    if (copy && copyDc) {
        HGDIOBJ old2 = SelectObject(copyDc, copy);
        BitBlt(copyDc, 0, 0, width, height, GetDC(owner), 0, 0, WHITENESS);
        Gdiplus::Graphics g2(copyDc);
        g2.DrawImage(bitmap, 0, 0, width, height);
        SelectObject(copyDc, old2);
    }

    if (OpenClipboard(owner)) {
        EmptyClipboard();
        if (global) {
            if (SetClipboardData(CF_DIB, global)) {
                global = nullptr;  // owned by the clipboard now
                ok = true;
            }
        }
        if (copy) {
            if (!SetClipboardData(CF_BITMAP, copy)) DeleteObject(copy);
        }
        CloseClipboard();
    }
    if (global) GlobalFree(global);
    if (copyDc) DeleteDC(copyDc);
    if (dib) DeleteObject(dib);
    return ok;
}

std::wstring defaultFileNameFor(const std::wstring& content) {
    std::wstring name;
    for (wchar_t c : content) {
        if (c == L'\r' || c == L'\n' || c == L'\t') {
            if (!name.empty() && name.back() != L'-') name.push_back(L'-');
            continue;
        }
        if (name.size() >= 40) break;
        bool invalid = wcschr(L"\\/:*?\"<>|", c) != nullptr;
        name.push_back(invalid ? L'-' : c);
    }
    while (!name.empty() && (name.back() == L'-' || name.back() == L'.' || name.back() == L' '))
        name.pop_back();
    if (name.empty()) name = L"qrcode";
    return name + L".png";
}
