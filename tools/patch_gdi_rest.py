# -*- coding: utf-8 -*-
"""Finish the GDI+ removal: popup, settings, main, build script."""
import io


def patch(path, pairs):
    text = io.open(path, encoding="utf-8-sig").read()
    for old, new in pairs:
        if old not in text:
            raise SystemExit("MISSING in %s:\n%s" % (path, old[:120]))
        text = text.replace(old, new, 1)
    io.open(path, "w", encoding="utf-8", newline="\n").write(text)
    print("patched", path.split("\\")[-1])


# ---------------------------------------------------------------- popup.cpp
patch(r"C:\PtFastQRcode\src\app\popup.cpp", [
    ("""    {
        Gdiplus::Graphics g(hdc);
        Gdiplus::Pen pen(toGdiColor(g_theme.cardBorder), 1.0f);
        g.DrawLine(&pen, (float)st->layout.separator.left, (float)st->layout.separator.top + 0.5f,
                   (float)st->layout.separator.right, (float)st->layout.separator.top + 0.5f);
    }""",
     """    {
        HPEN pen = CreatePen(PS_SOLID, 1, g_theme.cardBorder);
        HGDIOBJ previous = SelectObject(hdc, pen);
        MoveToEx(hdc, st->layout.separator.left, st->layout.separator.top, nullptr);
        LineTo(hdc, st->layout.separator.right + 1, st->layout.separator.top);
        SelectObject(hdc, previous);
        DeleteObject(pen);
    }"""),

    ("""    {
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        drawRoundedRect(g,
                        Gdiplus::RectF((float)st->layout.card.left + 0.5f,
                                       (float)st->layout.card.top + 0.5f,
                                       (float)(st->layout.card.right - st->layout.card.left) - 1.0f,
                                       (float)(st->layout.card.bottom - st->layout.card.top) - 1.0f),
                        (float)(g_theme.radius + 2), toGdiColor(g_theme.card),
                        toGdiColor(g_theme.cardBorder), 1.0f);

        RECT card = st->layout.card;
        if (st->hasSymbol) {
            drawQrSymbol(hdc, card, st->symbol);
        } else {""",
     """    {
        drawRoundRect(hdc, st->layout.card, g_theme.radius + 2, g_theme.card, g_theme.cardBorder, 1);

        RECT card = st->layout.card;
        if (st->hasSymbol) {
            drawQrSymbol(hdc, card, st->symbol, S(28));
        } else {"""),
])

# ---------------------------------------------------------------- settings_win.cpp
patch(r"C:\PtFastQRcode\src\app\settings_win.cpp", [
    ("""    Gdiplus::Graphics g(buffer.dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    COLORREF border = g_captureFocused ? g_theme.accent : g_theme.fieldBorder;
    drawRoundedRect(g, Gdiplus::RectF(0.5f, 0.5f, (float)client.right - 1.0f,
                                      (float)client.bottom - 1.0f),
                    (float)g_theme.radius, toGdiColor(g_theme.fieldBg), toGdiColor(border),
                    g_captureFocused ? 1.6f : 1.0f);""",
     """    COLORREF border = g_captureFocused ? g_theme.accent : g_theme.fieldBorder;
    drawRoundRect(buffer.dc, client, g_theme.radius, g_theme.fieldBg, border,
                  g_captureFocused ? 2 : 1);"""),

    ("""    {
        Gdiplus::Graphics g(hdc);
        Gdiplus::Pen pen(toGdiColor(g_theme.cardBorder), 1.0f);
        auto line = [&](int y) {
            g.DrawLine(&pen, (float)S(16), (float)y + 0.5f, (float)(clientWidth - S(16)),
                       (float)y + 0.5f);
        };
        line(S(48));
        line(clientHeight - S(64));
    }""",
     """    {
        HPEN pen = CreatePen(PS_SOLID, 1, g_theme.cardBorder);
        HGDIOBJ previous = SelectObject(hdc, pen);
        auto line = [&](int y) {
            MoveToEx(hdc, S(16), y, nullptr);
            LineTo(hdc, clientWidth - S(16) + 1, y);
        };
        line(S(48));
        line(clientHeight - S(64));
        SelectObject(hdc, previous);
        DeleteObject(pen);
    }"""),
])

# ---------------------------------------------------------------- main.cpp
patch(r"C:\PtFastQRcode\src\app\main.cpp", [
    ("""    Gdiplus::GdiplusStartupInput gdiplusInput;
    ULONG_PTR gdiplusToken = 0;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusInput, nullptr);

""", ""),
    ("""        if (mutex) CloseHandle(mutex);
        Gdiplus::GdiplusShutdown(gdiplusToken);
        OleUninitialize();""",
     """        if (mutex) CloseHandle(mutex);
        OleUninitialize();"""),
    ("""    destroyFonts();
    Gdiplus::GdiplusShutdown(gdiplusToken);
    OleUninitialize();""",
     """    destroyFonts();
    OleUninitialize();"""),
])

# ---------------------------------------------------------------- build.ps1
path = r"C:\PtFastQRcode\build.ps1"
raw = io.open(path, "rb").read()
text = raw.decode("utf-8-sig")
old = """    'kernel32.lib', 'user32.lib', 'gdi32.lib', 'gdiplus.lib', 'shell32.lib',
    'shlwapi.lib', 'ole32.lib', 'comctl32.lib', 'comdlg32.lib', 'uuid.lib',
    'advapi32.lib', 'uxtheme.lib'"""
new = """    'kernel32.lib', 'user32.lib', 'gdi32.lib', 'shell32.lib', 'shlwapi.lib',
    'ole32.lib', 'comctl32.lib', 'comdlg32.lib', 'uuid.lib', 'advapi32.lib',
    'uxtheme.lib', 'windowscodecs.lib'"""
assert old in text, "libs"
text = text.replace(old, new, 1)
io.open(path, "wb").write(b"\xef\xbb\xbf" + text.encode("utf-8"))
print("build.ps1: gdiplus out, windowscodecs in")
