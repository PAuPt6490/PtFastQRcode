# -*- coding: utf-8 -*-
"""Drop GDI+ from the public header and expose the GDI/WIC image API."""
import io

path = r"C:\PtFastQRcode\src\app\app.h"
text = io.open(path, encoding="utf-8-sig").read()


def sub(old, new, label):
    global text
    if old not in text:
        raise SystemExit("MISSING: " + label)
    text = text.replace(old, new, 1)


sub("""#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objidl.h>   // GDI+ headers need IStream from here when WIN32_LEAN_AND_MEAN is set
#include <gdiplus.h>""",
    """#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>""", "includes")

sub("""void drawRoundedRect(Gdiplus::Graphics& g, const Gdiplus::RectF& rect, float radius,
                     const Gdiplus::Color& fill, const Gdiplus::Color& stroke, float strokeWidth);
void drawTextGdi(HDC hdc, HFONT font, COLORREF color, const wchar_t* text, RECT rect,
                 UINT format);
int  textWidth(HDC hdc, HFONT font, const wchar_t* text);
Gdiplus::Color toGdiColor(COLORREF color);
Gdiplus::Color toGdiColor(COLORREF color, BYTE alpha);
void paintWindowBackdrop(HDC hdc, const RECT& client, bool rounded);""",
    """// Rounded rectangle drawn with GDI. CLR_NONE as fill = hollow, as stroke = no border.
void drawRoundRect(HDC hdc, const RECT& rc, int radius, COLORREF fill, COLORREF stroke,
                   int strokeWidth);
void drawTextGdi(HDC hdc, HFONT font, COLORREF color, const wchar_t* text, RECT rect,
                 UINT format);
int  textWidth(HDC hdc, HFONT font, const wchar_t* text);
void paintWindowBackdrop(HDC hdc, const RECT& client, bool rounded);""", "drawing api")

sub("""// Renders the symbol into a white PNG-ready bitmap with a 4 module quiet zone.
Gdiplus::Bitmap* renderQrBitmap(const qr::Symbol& symbol, int pixelSize);

// Paints the symbol directly into 'area' (no bitmap kept in memory).
void drawQrSymbol(HDC hdc, const RECT& area, const qr::Symbol& symbol);

bool saveBitmapAsPng(Gdiplus::Bitmap* bitmap, const std::wstring& path);
bool copyBitmapToClipboard(HWND owner, Gdiplus::Bitmap* bitmap);""",
    """// Paints the symbol into 'area' (a 4 module quiet zone is added inside the modules).
void drawQrSymbol(HDC hdc, const RECT& area, const qr::Symbol& symbol, int padding);

// Renders the symbol into a 32bpp DIB and writes it as PNG through WIC.
bool saveQrPng(const qr::Symbol& symbol, int pixelSize, const std::wstring& path);

// Renders the symbol into a 32bpp DIB and puts it on the clipboard (CF_DIB + CF_BITMAP).
bool copyQrToClipboard(HWND owner, const qr::Symbol& symbol, int pixelSize);""", "image api")

io.open(path, "w", encoding="utf-8", newline="\n").write(text)
print("app.h updated")
