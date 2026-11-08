/* Clipboard picture -> LAN link.
 *
 * A pasted picture is not something a QR code can carry usefully, so the popup shows a
 * link to it instead: the picture is re-encoded as PNG (longest side capped at
 * kPictureMaxPixels so a 4K screenshot does not become megabytes) and published to the
 * small HTTP server in src/net. Everything stays on the private network and dies after
 * five minutes. Nothing here touches the file system.
 *
 * A thumbnail of the same picture comes back with the link so the popup can show which
 * picture it is about to serve; it is capped at kPreviewMaxPixels and lives only while
 * the popup does. */

#include "app.h"

#include <cstring>


#include "net/httpd.h"

namespace {

// 'hasAlpha' reports whether the clipboard DIB actually carries transparency. Screenshots
// and most copied images do not, and knowing that up front lets the oversized path stretch
// straight from the bitmap instead of copying the whole picture out first (several MB).
HBITMAP bitmapFromClipboard(bool& hasAlpha) {
    hasAlpha = false;
    HBITMAP result = nullptr;
    if (!OpenClipboard(g_popup)) return nullptr;
    // CF_DIB is preferred: it carries 32bpp pixels with the alpha channel intact.
    HANDLE dib = GetClipboardData(CF_DIB);
    if (dib) {
        const BITMAPINFOHEADER* header = static_cast<const BITMAPINFOHEADER*>(GlobalLock(dib));
        if (header) {
            void* pixels = (void*)((const BYTE*)header + header->biSize + header->biClrUsed * 4u);
            if (header->biBitCount == 32) {
                const size_t count = (size_t)header->biWidth * (size_t)header->biHeight;
                const BYTE* bytes = static_cast<const BYTE*>(pixels);
                for (size_t i = 0; i < count; ++i) {
                    if (bytes[i * 4 + 3] != 255) {
                        hasAlpha = true;
                        break;
                    }
                }
            }
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

HBITMAP sectionOf(int width, int height, void** bits) {
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;  // top-down, row 0 on top
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    HDC screen = GetDC(nullptr);
    HBITMAP section = CreateDIBSection(screen, &info, DIB_RGB_COLORS, bits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    return section;
}

// 32bpp top-down pixels into a DIB holding exactly those pixels. GDI keeps the alpha
// bytes it is given, so semi transparent screenshots stay that way on screen.
HBITMAP bitmapFromPixels(const BYTE* pixels, int width, int height) {
    if (width <= 0 || height <= 0) return nullptr;
    void* bits = nullptr;
    HBITMAP bitmap = sectionOf(width, height, &bits);
    if (!bitmap) return nullptr;
    if (!bits) {
        DeleteObject(bitmap);
        return nullptr;
    }
    memcpy(bits, pixels, (size_t)width * (size_t)height * 4u);
    return bitmap;
}

// Stretches 32bpp top-down pixels down to targetWidth x targetHeight. Two DIB sections are
// needed because GDI cannot stretch straight out of a memory buffer; the scaled result is
// copied into 'out' before either handle goes away.
bool shrinkPixels(const BYTE* pixels, int width, int height, int targetWidth, int targetHeight,
                  std::vector<BYTE>& out) {
    if (width <= 0 || height <= 0 || targetWidth <= 0 || targetHeight <= 0) return false;

    void* fullBits = nullptr;
    void* smallBits = nullptr;
    HBITMAP fullSection = sectionOf(width, height, &fullBits);
    HBITMAP smallSection = fullSection ? sectionOf(targetWidth, targetHeight, &smallBits) : nullptr;

    bool scaled = false;
    if (fullSection && smallSection && fullBits && smallBits) {
        memcpy(fullBits, pixels, (size_t)width * (size_t)height * 4u);
        HDC fullDc = CreateCompatibleDC(nullptr);
        HDC smallDc = CreateCompatibleDC(nullptr);
        if (fullDc && smallDc) {
            HGDIOBJ oldFull = SelectObject(fullDc, fullSection);
            HGDIOBJ oldSmall = SelectObject(smallDc, smallSection);
            if (oldFull && oldFull != HGDI_ERROR && oldSmall && oldSmall != HGDI_ERROR) {
                SetStretchBltMode(smallDc, HALFTONE);
                SetBrushOrgEx(smallDc, 0, 0, nullptr);
                scaled = StretchBlt(smallDc, 0, 0, targetWidth, targetHeight, fullDc, 0, 0, width,
                                    height, SRCCOPY) != FALSE;
            }
            if (oldFull && oldFull != HGDI_ERROR) SelectObject(fullDc, oldFull);
            if (oldSmall && oldSmall != HGDI_ERROR) SelectObject(smallDc, oldSmall);
        }
        if (fullDc) DeleteDC(fullDc);
        if (smallDc) DeleteDC(smallDc);

        if (scaled) {
            out.assign((size_t)targetWidth * (size_t)targetHeight * 4u, 0);
            memcpy(out.data(), smallBits, out.size());

            // StretchBlt carries the colour but leaves the alpha byte behind (GDI does not
            // manage alpha), so the scaled copy rebuilds it from the source. Opaque pictures
            // stay opaque - otherwise the PNG would encode as a blank square - and real
            // transparency keeps a nearest neighbour copy of its coverage.
            const size_t sourceBytes = (size_t)width * (size_t)height * 4u;
            bool opaque = true;
            for (size_t i = 3; i < sourceBytes; i += 4) {
                if (pixels[i] != 255) {
                    opaque = false;
                    break;
                }
            }
            if (opaque) {
                for (size_t i = 3; i < out.size(); i += 4) out[i] = 255;
            } else {
                for (int y = 0; y < targetHeight; ++y) {
                    // 32-bit arithmetic on purpose: 'long long' would pull __alldiv from the
                    // CRT, which this build does not link. Pixel counts stay far below 2^31.
                    const int sourceY = (int)((long)y * height / targetHeight);
                    for (int x = 0; x < targetWidth; ++x) {
                        const int sourceX = (int)((long)x * width / targetWidth);
                        const size_t from = ((size_t)sourceY * (size_t)width + (size_t)sourceX) * 4u;
                        const size_t to = ((size_t)y * (size_t)targetWidth + (size_t)x) * 4u;
                        out[to + 3] = pixels[from + 3];
                    }
                }
            }
        }
    }
    if (fullSection) DeleteObject(fullSection);
    if (smallSection) DeleteObject(smallSection);
    return scaled;
}

// Stretches a bitmap straight into fresh 32bpp pixels. No intermediate copy of the full
// size picture exists, which matters: a 4K screenshot is over 30 MB of pixels.
bool stretchBitmap(HBITMAP source, int width, int height, int targetWidth, int targetHeight,
                   std::vector<BYTE>& out) {
    if (width <= 0 || height <= 0 || targetWidth <= 0 || targetHeight <= 0) return false;

    void* bits = nullptr;
    HBITMAP section = sectionOf(targetWidth, targetHeight, &bits);
    if (!section) return false;
    if (!bits) {
        DeleteObject(section);
        return false;
    }

    bool scaled = false;
    HDC sourceDc = CreateCompatibleDC(nullptr);
    HDC targetDc = CreateCompatibleDC(nullptr);
    if (sourceDc && targetDc) {
        HGDIOBJ oldSource = SelectObject(sourceDc, source);
        HGDIOBJ oldTarget = SelectObject(targetDc, section);
        if (oldSource && oldSource != HGDI_ERROR && oldTarget && oldTarget != HGDI_ERROR) {
            SetStretchBltMode(targetDc, HALFTONE);
            SetBrushOrgEx(targetDc, 0, 0, nullptr);
            scaled = StretchBlt(targetDc, 0, 0, targetWidth, targetHeight, sourceDc, 0, 0, width,
                                height, SRCCOPY) != FALSE;
        }
        if (oldSource && oldSource != HGDI_ERROR) SelectObject(sourceDc, oldSource);
        if (oldTarget && oldTarget != HGDI_ERROR) SelectObject(targetDc, oldTarget);
    }
    if (sourceDc) DeleteDC(sourceDc);
    if (targetDc) DeleteDC(targetDc);

    if (scaled) {
        out.assign((size_t)targetWidth * (size_t)targetHeight * 4u, 0);
        memcpy(out.data(), bits, out.size());
        // GDI does not manage alpha and leaves it at zero; this path only runs for pictures
        // without transparency, so every pixel is opaque.
        for (size_t i = 3; i < out.size(); i += 4) out[i] = 255;
    }
    DeleteObject(section);
    return scaled;
}

// 32bpp top-down pixels of 'source' at or below the pixel cap. GetDIBits hides the
// orientation, which matters: the encoder wants row 0 on top. The result always lives in
// 'storage', never in GDI-owned memory, so nothing can be freed underneath the encoder.
// 'keepAlpha' selects the slow path, needed only for pictures with real transparency.
bool downscaledPixels(HBITMAP source, bool keepAlpha, int& width, int& height,
                      std::vector<BYTE>& storage) {
    BITMAP description = {};
    if (!GetObjectW(source, sizeof(description), &description)) return false;
    width = description.bmWidth;
    height = description.bmHeight;
    if (width <= 0 || height <= 0) return false;

    const bool oversized = width > kPictureMaxPixels || height > kPictureMaxPixels;

    if (oversized && !keepAlpha) {
        const int longest = width > height ? width : height;
        const float factor = (float)kPictureMaxPixels / (float)longest;
        const int smallWidth = (int)(width * factor + 0.5f);
        const int smallHeight = (int)(height * factor + 0.5f);
        if (!stretchBitmap(source, width, height, smallWidth, smallHeight, storage)) return false;
        width = smallWidth;
        height = smallHeight;
        return true;
    }

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

    if (!oversized) return true;

    const int longest = width > height ? width : height;
    const float factor = (float)kPictureMaxPixels / (float)longest;
    const int smallWidth = (int)(width * factor + 0.5f);
    const int smallHeight = (int)(height * factor + 0.5f);

    std::vector<BYTE> small;
    if (!shrinkPixels(storage.data(), width, height, smallWidth, smallHeight, small)) return false;
    storage.swap(small);
    width = smallWidth;
    height = smallHeight;
    return true;
}

// Thumbnail of the already downscaled picture: the popup draws it a couple of hundred
// pixels wide, so keeping the 720 px copy around would only waste memory.
bool buildPreview(const std::vector<BYTE>& pixels, int width, int height, int linkWidth,
                  int linkHeight, int sourceWidth, int sourceHeight, PicturePreview& preview) {
    const int longest = width > height ? width : height;
    int targetWidth = width;
    int targetHeight = height;
    if (longest > kPreviewMaxPixels) {
        const float factor = (float)kPreviewMaxPixels / (float)longest;
        targetWidth = (int)(width * factor + 0.5f);
        targetHeight = (int)(height * factor + 0.5f);
    }
    if (targetWidth < 1) targetWidth = 1;
    if (targetHeight < 1) targetHeight = 1;

    std::vector<BYTE> small;
    const BYTE* source = pixels.data();
    if (targetWidth != width || targetHeight != height) {
        if (!shrinkPixels(pixels.data(), width, height, targetWidth, targetHeight, small))
            return false;
        source = small.data();
    }

    HBITMAP bitmap = bitmapFromPixels(source, targetWidth, targetHeight);
    if (!bitmap) return false;

    preview.bitmap = bitmap;
    preview.width = targetWidth;
    preview.height = targetHeight;
    preview.sourceWidth = sourceWidth;
    preview.sourceHeight = sourceHeight;
    preview.linkWidth = linkWidth;
    preview.linkHeight = linkHeight;
    return true;
}

}  // namespace

bool clipboardHasPicture() {
    const bool has = IsClipboardFormatAvailable(CF_DIB) || IsClipboardFormatAvailable(CF_BITMAP);
    return has;
}

void releasePicturePreview(PicturePreview& preview) {
    if (preview.bitmap) DeleteObject(preview.bitmap);
    preview.bitmap = nullptr;
    preview.width = 0;
    preview.height = 0;
    preview.sourceWidth = 0;
    preview.sourceHeight = 0;
    preview.linkWidth = 0;
    preview.linkHeight = 0;
}

bool publishClipboardPicture(std::wstring& url, PicturePreview& preview) {
    url.clear();
    releasePicturePreview(preview);
    if (!net::running()) {
        return false;
    }

    bool hasAlpha = false;
    HBITMAP bitmap = bitmapFromClipboard(hasAlpha);
    if (!bitmap) {
        return false;
    }

    int sourceWidth = 0;
    int sourceHeight = 0;
    BITMAP description = {};
    if (GetObjectW(bitmap, sizeof(description), &description)) {
        sourceWidth = description.bmWidth;
        sourceHeight = description.bmHeight;
    }

    int width = 0;
    int height = 0;
    std::vector<BYTE> storage;
    const bool scaled = downscaledPixels(bitmap, hasAlpha, width, height, storage);
    DeleteObject(bitmap);

    std::string png;
    if (!scaled || !net::encodePixelsAsPng(storage.data(), width, height, png)) return false;

    bool published = false;
    const std::wstring path = net::publish(png.data(), png.size(), net::Kind::Png, published);
    if (!published) return false;

    // The thumbnail is best effort: a link without a preview still works.
    buildPreview(storage, width, height, width, height, sourceWidth, sourceHeight, preview);

    // Same path as the PNG: keep the token, drop the ".png" so the QR code opens the page.
    const size_t tokenLength = path.size() > 4 ? path.size() - 4 : path.size();
    url = net::pageUrl(path.substr(0, tokenLength));
    return true;
}
