# -*- coding: utf-8 -*-
"""Settings window polish: section hairlines, consistent spacing, dirty-region painting."""
import io

path = r"C:\PtFastQRcode\src\app\settings_win.cpp"
text = io.open(path, encoding="utf-8-sig").read()


def sub(old, new, label):
    global text
    if old not in text:
        raise SystemExit("MISSING: " + label)
    text = text.replace(old, new, 1)


# ---- 1. section hairlines + version line -------------------------------------
sub("""    {
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
    }""",
    """    {
        // Header rule, one hairline per section, and the footer rule.
        HPEN pen = CreatePen(PS_SOLID, 1, g_theme.cardBorder);
        HGDIOBJ previous = SelectObject(hdc, pen);
        auto line = [&](int y) {
            MoveToEx(hdc, S(16), y, nullptr);
            LineTo(hdc, clientWidth - S(16) + 1, y);
        };
        line(S(48));
        line(S(128));
        line(S(284));
        line(S(476));
        line(clientHeight - S(64));
        SelectObject(hdc, previous);
        DeleteObject(pen);
    }""", "hairlines")

# ---- 2. tighten label positions to the new rules -----------------------------
sub("""    label(L"行为", 16, 132, 200, 20, g_fonts.bold, g_theme.text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);""",
    """    label(L"行为", 16, 134, 200, 20, g_fonts.bold, g_theme.text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);""",
    "behaviour label")

sub("""    label(L"二维码", 16, 292, 200, 20, g_fonts.bold, g_theme.text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);""",
    """    label(L"二维码", 16, 290, 200, 20, g_fonts.bold, g_theme.text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);""",
    "qr label")

sub("""    label(L"界面风格", 16, 484, 200, 18, g_fonts.hint, g_theme.textDim,
          DT_LEFT | DT_VCENTER | DT_SINGLELINE);""",
    """    label(L"界面风格", 16, 482, 200, 18, g_fonts.hint, g_theme.textDim,
          DT_LEFT | DT_VCENTER | DT_SINGLELINE);""", "style label")

# ---- 3. version stamp in the footer -----------------------------------------
sub("""    label(L"Win32 走系统主题引擎;WinUI 3 用系统主色与官方图标字体;点“保存并重启”立即生效。",
          16, 594, clientWidth - S(32), 18, g_fonts.hint, g_theme.textDim,
          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);""",
    """    label(L"Win32 走系统主题引擎;WinUI 3 用系统主色与官方图标字体;点“保存并重启”立即生效。",
          16, 594, clientWidth - S(180), 18, g_fonts.hint, g_theme.textDim,
          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    label(kAppVersion, clientWidth - S(160), 594, S(144), 18, g_fonts.hint, g_theme.textDim,
          DT_RIGHT | DT_VCENTER | DT_SINGLELINE);""", "version")

# ---- 4. dirty-region painting ----------------------------------------------
sub("void paintSettings(HWND hwnd, HDC hdc) {",
    "void paintSettings(HWND hwnd, HDC hdc, const RECT& dirty) {", "signature")

sub("""            if (paintIsBuffered()) {
                RECT client;
                GetClientRect(hwnd, &client);
                PaintBuffer buffer = beginPaintBuffer(hwnd, client);
                paintSettings(hwnd, buffer.dc);
                endPaintBuffer(hwnd, buffer);
            } else {
                paintSettings(hwnd, dc);
            }""",
    """            if (paintIsBuffered()) {
                RECT client;
                GetClientRect(hwnd, &client);
                PaintBuffer buffer = beginPaintBuffer(hwnd, client);
                paintSettings(hwnd, buffer.dc, ps.rcPaint);
                endPaintBuffer(hwnd, buffer);
            } else {
                paintSettings(hwnd, dc, ps.rcPaint);
            }""", "paint calls")

# The whole surface is painted from one backdrop pass; separate label groups only when dirty.
sub("""    RECT client;
    GetClientRect(hwnd, &client);
    paintWindowBackdrop(hdc, client, true);""",
    """    RECT client;
    GetClientRect(hwnd, &client);
    paintWindowBackdrop(hdc, client, true);
    if (!rectsOverlap(dirty, client)) return;""", "early out")

io.open(path, "w", encoding="utf-8", newline="\n").write(text)
print("settings_win.cpp polished")
