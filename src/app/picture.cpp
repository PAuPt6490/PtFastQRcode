/* Clipboard picture -> LAN link.
 *
 * A pasted picture is not something a QR code can carry usefully, so the popup shows a
 * link to it instead: the picture is re-encoded as PNG (longest side capped at
 * kPictureMaxPixels so a 4K screenshot does not become megabytes) and published to the
 * small HTTP server in src/net. Everything stays on the private network and dies after
 * five minutes. Nothing here touches the file system. */

#include "app.h"

#include <cstring>


#include "net/httpd.h"

namespace {

HBITMAP bitmapFromClipboard() {
    HBITMAP result = nullptr;
    if (!OpenClipboard(g_popup)) return nullptr;
    // CF_DIB is preferred: it carries 32bpp pixels with the alpha channel intact.
    HANDLE dib = GetClipboardData(CF_DIB);
    if (dib) {
        const BITMAPINFOHEADER* header = static_cast<const BITMAPINFOHEADER*>(GlobalLock(dib));
        if (header) {
            void* pixels = (void*)((const BYTE*)header + header->biSize + header->biClrUsed * 4u);
            HDC screen = GetDC(nullptr);
            result = CreateDIBitmap(screen, header, CBM_INIT, pixels,
                                    reinterpret_cast<const BITMAPINFO*>(header), DIB_RGB_COLORS);
            ReleaseDC(nullptr, screen);
            GlobalUnlock(dib);
        }
    }
    if (!result) {
        HANDLE handle = GetClipboardData(CF_BITMAP);
        if (handle) result = (HBITMAP)CopyImage((HBITMAP)handle, IMAGE_BITMAP, 0, 0, 0);
    }
    CloseClipboard();
    return result;
}

// 32bpp top-down pixels of 'source' at or below the pixel cap. GetDIBits hides the
// orientation, which matters: the encoder wants row 0 on top. The result always lives in
// 'storage', never in GDI-owned memory, so nothing can be freed underneath the encoder.
bool downscaledPixels(HBITMAP source, int& width, int& height, std::vector<BYTE>& storage) {
    BITMAP description = {};
    if (!GetObjectW(source, sizeof(description), &description)) return false;
    width = description.bmWidth;
    height = description.bmHeight;
    if (width <= 0 || height <= 0) return false;

    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;  // top-down
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    storage.assign((size_t)width * (size_t)height * 4u, 0);
    HDC screen = GetDC(nullptr);
    const int rows = GetDIBits(screen, source, 0, (UINT)height, storage.data(), &info,
                               DIB_RGB_COLORS);
    ReleaseDC(nullptr, screen);
    if (rows == 0 || rows == (int)GDI_ERROR) return false;

    // GDI frequently leaves the alpha channel at zero; anything fully transparent in that
    // case was meant to be opaque, otherwise the PNG would come out blank.
    bool anyAlpha = false;
    for (size_t i = 3; i < storage.size(); i += 4) {
        if (storage[i] != 0) {
            anyAlpha = true;
            break;
        }
    }
    if (!anyAlpha) {
        for (size_t i = 3; i < storage.size(); i += 4) storage[i] = 255;
    }

    if (width <= kPictureMaxPixels && height <= kPictureMaxPixels) return true;

    const float factor = (float)kPictureMaxPixels / (float)(width > height ? width : height);
    const int smallWidth = (int)(width * factor + 0.5f);
    const int smallHeight = (int)(height * factor + 0.5f);
    if (smallWidth <= 0 || smallHeight <= 0) return false;

    auto makeSection = [](int w, int h, void** bits) -> HBITMAP {
        BITMAPINFO target = {};
        target.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        target.bmiHeader.biWidth = w;
        target.bmiHeader.biHeight = -h;  // top-down
        target.bmiHeader.biPlanes = 1;
        target.bmiHeader.biBitCount = 32;
        target.bmiHeader.biCompression = BI_RGB;
        HDC screenDc = GetDC(nullptr);
        HBITMAP section = CreateDIBSection(screenDc, &target, DIB_RGB_COLORS, bits, nullptr, 0);
        ReleaseDC(nullptr, screenDc);
        return section;
    };

    // Two DIB sections: the source copy (GDI has no way to stretch straight out of a
    // memory buffer) and the halved target. The scaled pixels are copied back into
    // 'storage' before either handle is deleted.
    void* fullBits = nullptr;
    void* smallBits = nullptr;
    HBITMAP fullSection = makeSection(width, height, &fullBits);
    HBITMAP smallSection = fullSection ? makeSection(smallWidth, smallHeight, &smallBits) : nullptr;
    bool scaledOk = false;
    if (fullSection && smallSection && fullBits && smallBits) {
        memcpy(fullBits, storage.data(), storage.size());
        HDC fullDc = CreateCompatibleDC(nullptr);
        HDC smallDc = CreateCompatibleDC(nullptr);
        if (fullDc && smallDc) {
            HGDIOBJ oldFull = SelectObject(fullDc, fullSection);
            HGDIOBJ oldSmall = SelectObject(smallDc, smallSection);
            if (oldFull && oldFull != HGDI_ERROR && oldSmall && oldSmall != HGDI_ERROR) {
                SetStretchBltMode(smallDc, HALFTONE);
                SetBrushOrgEx(smallDc, 0, 0, nullptr);
                scaledOk = StretchBlt(smallDc, 0, 0, smallWidth, smallHeight, fullDc, 0, 0, width,
                                      height, SRCCOPY) != FALSE;
            }
            if (oldFull && oldFull != HGDI_ERROR) SelectObject(fullDc, oldFull);
            if (oldSmall && oldSmall != HGDI_ERROR) SelectObject(smallDc, oldSmall);
        }
        if (fullDc) DeleteDC(fullDc);
        if (smallDc) DeleteDC(smallDc);

        if (scaledOk) {
            const size_t smallBytes = (size_t)smallWidth * (size_t)smallHeight * 4u;
            storage.assign(smallBytes, 0);
            memcpy(storage.data(), smallBits, smallBytes);
            width = smallWidth;
            height = smallHeight;
        }
    }
    if (fullSection) DeleteObject(fullSection);
    if (smallSection) DeleteObject(smallSection);
    return scaledOk;
}

}  // namespace

bool clipboardHasPicture() {
    const bool has = IsClipboardFormatAvailable(CF_DIB) || IsClipboardFormatAvailable(CF_BITMAP);
    return has;
}

bool publishClipboardPicture(std::wstring& url) {
    url.clear();
    if (!net::running()) {
        return false;
    }

    HBITMAP bitmap = bitmapFromClipboard();
    if (!bitmap) {
        return false;
    }

    int width = 0;
    int height = 0;
    std::vector<BYTE> storage;
    const bool scaled = downscaledPixels(bitmap, width, height, storage);
    std::string png;
    const bool encoded = scaled && net::encodePixelsAsPng(storage.data(), width, height, png);
    DeleteObject(bitmap);
    if (!encoded) return false;

    bool published = false;
    const std::wstring path = net::publish(png.data(), png.size(), net::Kind::Png, published);
    if (!published) return false;
    // Same path as the PNG: keep the token, drop the ".png" so the QR code opens the page.
    const size_t tokenLength = path.size() > 4 ? path.size() - 4 : path.size();
    url = net::pageUrl(path.substr(0, tokenLength));
    return true;
}
