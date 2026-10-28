# -*- coding: utf-8 -*-
"""Skip painting sections that are outside the invalidated region."""
import io

path = r"C:\PtFastQRcode\src\app\popup.cpp"
text = io.open(path, encoding="utf-8-sig").read()


def sub(old, new, label):
    global text
    if old not in text:
        raise SystemExit("MISSING: " + label)
    text = text.replace(old, new, 1)


sub("""void paintPopup(HWND hwnd, HDC hdc) {
    PopupState* st = getState(hwnd);
    if (!st) return;

    RECT client;
    GetClientRect(hwnd, &client);

    paintWindowBackdrop(hdc, client, true);

    if (st->icon) {""",
    """// Paints only the parts that intersect the invalidated rectangle: when a status line
// changes there is no reason to redraw the QR matrix on top of it.
void paintPopup(HWND hwnd, HDC hdc, const RECT& dirty) {
    PopupState* st = getState(hwnd);
    if (!st) return;

    RECT client;
    GetClientRect(hwnd, &client);

    paintWindowBackdrop(hdc, client, true);

    const bool headerDirty = tu::overlaps(dirty, st->layout.separator);
    const bool fieldDirty = tu::overlaps(dirty, st->layout.field);
    const bool cardDirty = tu::overlaps(dirty, st->layout.card);
    const bool statusDirty = tu::overlaps(dirty, st->layout.status);

    if (headerDirty && st->icon) {""", "signature")

sub("""    RECT titleRect = st->layout.title;
    drawTextGdi(hdc, g_fonts.title, g_theme.text, L"PtFastQRcode", titleRect,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    std::wstring subtitle = formatHotkey(g_settings.hotkeyMods, g_settings.hotkeyVk) + L"  唤起";
    RECT subtitleRect = st->layout.subtitle;
    drawTextGdi(hdc, g_fonts.hint, g_theme.textDim, subtitle.c_str(), subtitleRect,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    {
        HPEN pen = CreatePen(PS_SOLID, 1, g_theme.cardBorder);
        HGDIOBJ previous = SelectObject(hdc, pen);
        MoveToEx(hdc, st->layout.separator.left, st->layout.separator.top, nullptr);
        LineTo(hdc, st->layout.separator.right + 1, st->layout.separator.top);
        SelectObject(hdc, previous);
        DeleteObject(pen);
    }

    RECT labelRect = st->layout.contentLabel;
    drawTextGdi(hdc, g_fonts.hint, g_theme.textDim, L"内容", labelRect,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    paintFieldFrame(hwnd, hdc, st->layout.field, g_theme.radius);

    // ---- QR card ----
    {""",
    """    if (headerDirty) {
        RECT titleRect = st->layout.title;
        drawTextGdi(hdc, g_fonts.title, g_theme.text, L"PtFastQRcode", titleRect,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        std::wstring subtitle =
            formatHotkey(g_settings.hotkeyMods, g_settings.hotkeyVk) + L"  唤起";
        RECT subtitleRect = st->layout.subtitle;
        drawTextGdi(hdc, g_fonts.hint, g_theme.textDim, subtitle.c_str(), subtitleRect,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        HPEN pen = CreatePen(PS_SOLID, 1, g_theme.cardBorder);
        HGDIOBJ previous = SelectObject(hdc, pen);
        MoveToEx(hdc, st->layout.separator.left, st->layout.separator.top, nullptr);
        LineTo(hdc, st->layout.separator.right + 1, st->layout.separator.top);
        SelectObject(hdc, previous);
        DeleteObject(pen);
    }

    if (fieldDirty) {
        RECT labelRect = st->layout.contentLabel;
        drawTextGdi(hdc, g_fonts.hint, g_theme.textDim, L"内容", labelRect,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        paintFieldFrame(hwnd, hdc, st->layout.field, g_theme.radius);
    }

    // ---- QR card ----
    if (cardDirty) {""", "sections")

sub("""    std::wstring status = st->message.empty() ? defaultStatusText() : st->message;
    COLORREF statusColor = st->messageIsError ? g_theme.danger
                                              : (st->message.empty() ? g_theme.textDim : g_theme.text);
    RECT statusRect = st->layout.status;
    drawTextGdi(hdc, g_fonts.hint, statusColor, status.c_str(), statusRect,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    RECT hintRect = st->layout.hint;
    drawTextGdi(hdc, g_fonts.hint, g_theme.textDim,
                L"Esc 关闭    Ctrl+S 保存    Ctrl+D 复制图片", hintRect,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}""",
    """    if (statusDirty) {
        std::wstring status = st->message.empty() ? defaultStatusText() : st->message;
        COLORREF statusColor = st->messageIsError
                                   ? g_theme.danger
                                   : (st->message.empty() ? g_theme.textDim : g_theme.text);
        RECT statusRect = st->layout.status;
        drawTextGdi(hdc, g_fonts.hint, statusColor, status.c_str(), statusRect,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    if (tu::overlaps(dirty, st->layout.hint)) {
        RECT hintRect = st->layout.hint;
        drawTextGdi(hdc, g_fonts.hint, g_theme.textDim,
                    L"Esc 关闭    Ctrl+S 保存    Ctrl+D 复制图片", hintRect,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}""", "status/hint")

# WM_PAINT call sites pass the invalid rectangle
sub("""                paintPopup(hwnd, buffer.dc);""",
    """                paintPopup(hwnd, buffer.dc, ps.rcPaint);""", "paint buffered")
sub("""                paintPopup(hwnd, dc);""",
    """                paintPopup(hwnd, dc, ps.rcPaint);""", "paint direct")

io.open(path, "w", encoding="utf-8", newline="\n").write(text)
print("popup.cpp: dirty-region painting")
