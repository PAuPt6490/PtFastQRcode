#include "app.h"

#include <commdlg.h>
#include <windowsx.h>

#include <algorithm>
#include <string>

#include "resource.h"

namespace {

constexpr int IDC_EDIT = 2001;
constexpr int IDC_BTN_SAVE = ID_POPUP_SAVE;
constexpr int IDC_BTN_COPY = ID_POPUP_COPY;
constexpr int IDC_BTN_PASTE = 2004;
constexpr int IDC_BTN_SETTINGS = 2005;
constexpr int IDC_BTN_CLOSE = ID_POPUP_CLOSE;

constexpr UINT TIMER_DEBOUNCE = 1;
constexpr UINT TIMER_STATUS = 2;
constexpr UINT TIMER_HIDE = 3;

const BtnSpec kButtons[] = {
    {IDC_BTN_SAVE, BtnKind::Primary, L"保存图片"},
    {IDC_BTN_COPY, BtnKind::Secondary, L"复制图片"},
    {IDC_BTN_PASTE, BtnKind::Secondary, L"粘贴"},
    {IDC_BTN_SETTINGS, BtnKind::IconSliders, L""},
    {IDC_BTN_CLOSE, BtnKind::IconClose, L""},
};

struct Layout {
    RECT headerIcon;
    RECT title;
    RECT subtitle;
    RECT separator;
    RECT contentLabel;
    RECT field;       // painted rounded field
    RECT edit;        // real EDIT control
    RECT card;
    RECT status;
    RECT hint;
    RECT btnSave;
    RECT btnCopy;
    RECT btnPaste;
    RECT btnSettings;
    RECT btnClose;
};

struct PopupState {
    HWND edit = nullptr;
    HWND btnSave = nullptr;
    HWND btnCopy = nullptr;
    HWND btnPaste = nullptr;
    HWND btnSettings = nullptr;
    HWND btnClose = nullptr;

    HICON icon = nullptr;
    HBRUSH editBrush = nullptr;

    qr::Symbol symbol;
    bool hasSymbol = false;
    std::wstring error;

    std::wstring message;
    bool messageIsError = false;
    bool suppressDeactivate = false;

    Layout layout;
};

PopupState* getState(HWND hwnd) {
    return reinterpret_cast<PopupState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

const BtnSpec* findSpec(int id) {
    for (const BtnSpec& spec : kButtons) {
        if (spec.id == id) return &spec;
    }
    return nullptr;
}

std::wstring getEditText(HWND edit) {
    int length = GetWindowTextLengthW(edit);
    if (length <= 0) return std::wstring();
    std::wstring text((size_t)length + 1, L'\0');
    int copied = GetWindowTextW(edit, &text[0], length + 1);
    text.resize((size_t)std::max(0, copied));
    return text;
}

std::wstring getClipboardText() {
    std::wstring result;
    if (!OpenClipboard(g_popup)) return result;
    HANDLE data = GetClipboardData(CF_UNICODETEXT);
    if (data) {
        const wchar_t* text = (const wchar_t*)GlobalLock(data);
        if (text) {
            result = text;
            GlobalUnlock(data);
        }
    }
    CloseClipboard();
    return result;
}

std::wstring defaultStatusText() {
    return g_settings.closeOnBlur ? L"点击窗口外自动关闭  ·  Esc 退出"
                                  : L"点击窗口外不关闭  ·  Esc 退出";
}

std::wstring baseName(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

std::wstring directoryOf(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

constexpr int kWidth = 400;
constexpr int kHeight = 470;
constexpr int kMinWidth = 340;
constexpr int kMinHeight = 400;

// The popup is freely resizable: the header, text field and buttons keep their heights,
// while the QR card takes every remaining pixel.
void layoutControls(HWND hwnd, PopupState* st) {
    RECT client = {0, 0, S(kWidth), S(kHeight)};
    GetClientRect(hwnd, &client);
    const int width = client.right;
    const int height = client.bottom;
    Layout& l = st->layout;

    l.headerIcon = {S(14), S(12), S(38), S(36)};
    l.title = {S(46), S(4), width - S(100), S(26)};
    l.subtitle = {S(46), S(24), width - S(100), S(44)};
    l.separator = {S(14), S(50), width - S(14), S(51)};

    l.contentLabel = {S(14), S(54), width - S(14), S(68)};
    l.field = {S(14), S(70), width - S(14), S(128)};
    l.edit = {S(21), S(77), width - S(21), S(121)};

    l.card = {S(14), S(134), width - S(14), height - S(100)};
    if (l.card.bottom < l.card.top + S(80)) l.card.bottom = l.card.top + S(80);

    l.status = {S(16), height - S(96), width - S(16), height - S(78)};
    l.hint = {0, height - S(30), width, height - S(14)};

    const int buttonTop = height - S(72);
    const int buttonBottom = height - S(36);
    l.btnSave = {S(14), buttonTop, S(154), buttonBottom};
    l.btnCopy = {S(162), buttonTop, S(262), buttonBottom};
    l.btnPaste = {S(270), buttonTop, width - S(14), buttonBottom};

    l.btnSettings = {width - S(82), S(10), width - S(52), S(40)};
    l.btnClose = {width - S(46), S(10), width - S(16), S(40)};

    auto place = [&](HWND control, const RECT& r) {
        if (control)
            SetWindowPos(control, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
    };
    place(st->edit, l.edit);
    place(st->btnSave, l.btnSave);
    place(st->btnCopy, l.btnCopy);
    place(st->btnPaste, l.btnPaste);
    place(st->btnSettings, l.btnSettings);
    place(st->btnClose, l.btnClose);
}

void setStatus(HWND hwnd, PopupState* st, const std::wstring& text, bool isError) {
    st->message = text;
    st->messageIsError = isError;
    InvalidateRect(hwnd, &st->layout.status, FALSE);
    if (!text.empty()) SetTimer(hwnd, TIMER_STATUS, 3200, nullptr);
}

void refreshQr(HWND hwnd) {
    PopupState* st = getState(hwnd);
    if (!st) return;

    st->hasSymbol = false;
    st->error.clear();

    std::wstring text = getEditText(st->edit);
    if (!text.empty()) {
        qr::EncodeOptions options;
        options.ecc = (qr::Ecc)std::max(0, std::min(3, g_settings.eccLevel));
        std::wstring error;
        if (qr::encodeText(text, options, st->symbol, error)) {
            st->hasSymbol = true;
        } else {
            st->error = error.empty() ? L"无法生成二维码" : error;
        }
    }
    InvalidateRect(hwnd, &st->layout.card, FALSE);
}

void paintPopup(HWND hwnd, HDC hdc) {
    PopupState* st = getState(hwnd);
    if (!st) return;

    RECT client;
    GetClientRect(hwnd, &client);

    paintWindowBackdrop(hdc, client, true);

    if (st->icon) {
        DrawIconEx(hdc, st->layout.headerIcon.left, st->layout.headerIcon.top, st->icon,
                   st->layout.headerIcon.right - st->layout.headerIcon.left,
                   st->layout.headerIcon.bottom - st->layout.headerIcon.top, 0, nullptr,
                   DI_NORMAL);
    }

    RECT titleRect = st->layout.title;
    drawTextGdi(hdc, g_fonts.title, g_theme.text, L"PtFastQRcode", titleRect,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    std::wstring subtitle = formatHotkey(g_settings.hotkeyMods, g_settings.hotkeyVk) + L"  唤起";
    RECT subtitleRect = st->layout.subtitle;
    drawTextGdi(hdc, g_fonts.hint, g_theme.textDim, subtitle.c_str(), subtitleRect,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    {
        Gdiplus::Graphics g(hdc);
        Gdiplus::Pen pen(toGdiColor(g_theme.cardBorder), 1.0f);
        g.DrawLine(&pen, (float)st->layout.separator.left, (float)st->layout.separator.top + 0.5f,
                   (float)st->layout.separator.right, (float)st->layout.separator.top + 0.5f);
    }

    RECT labelRect = st->layout.contentLabel;
    drawTextGdi(hdc, g_fonts.hint, g_theme.textDim, L"内容", labelRect,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    paintFieldFrame(hwnd, hdc, st->layout.field, g_theme.radius);

    // ---- QR card ----
    {
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
        } else {
            std::wstring placeholder = st->error.empty() ? L"粘贴或输入内容后自动生成" : st->error;
            RECT textRect = card;
            textRect.left += S(24);
            textRect.right -= S(24);
            drawTextGdi(hdc, g_fonts.body,
                        st->error.empty() ? g_theme.textDim : g_theme.danger, placeholder.c_str(),
                        textRect, DT_CENTER | DT_VCENTER | DT_WORDBREAK);
        }
    }

    std::wstring status = st->message.empty() ? defaultStatusText() : st->message;
    COLORREF statusColor = st->messageIsError ? g_theme.danger
                                              : (st->message.empty() ? g_theme.textDim : g_theme.text);
    RECT statusRect = st->layout.status;
    drawTextGdi(hdc, g_fonts.hint, statusColor, status.c_str(), statusRect,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    RECT hintRect = st->layout.hint;
    drawTextGdi(hdc, g_fonts.hint, g_theme.textDim,
                L"Esc 关闭    Ctrl+S 保存    Ctrl+D 复制图片", hintRect,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void savePng(HWND hwnd) {
    PopupState* st = getState(hwnd);
    if (!st) return;
    if (!st->hasSymbol) {
        setStatus(hwnd, st, L"请先输入要生成二维码的内容", true);
        return;
    }

    std::wstring text = getEditText(st->edit);
    std::wstring directory = g_settings.saveDir;
    ensureDirectory(directory);
    std::wstring suggested = directory + L"\\" + defaultFileNameFor(text);

    std::vector<wchar_t> buffer(MAX_PATH * 4, 0);
    wcsncpy_s(buffer.data(), buffer.size(), suggested.c_str(), _TRUNCATE);

    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = hwnd;
    dialog.lpstrFilter = L"PNG 图片 (*.png)\0*.png\0所有文件 (*.*)\0*.*\0";
    dialog.lpstrFile = buffer.data();
    dialog.nMaxFile = (DWORD)buffer.size();
    dialog.lpstrDefExt = L"png";
    dialog.lpstrInitialDir = directory.c_str();
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    st->suppressDeactivate = true;
    BOOL accepted = GetSaveFileNameW(&dialog);
    st->suppressDeactivate = false;
    if (!accepted) return;

    std::wstring path = buffer.data();
    Gdiplus::Bitmap* bitmap = renderQrBitmap(st->symbol, S(g_settings.qrSize));
    if (!bitmap) {
        setStatus(hwnd, st, L"生成图片失败", true);
        return;
    }
    bool saved = saveBitmapAsPng(bitmap, path);
    delete bitmap;

    if (saved) {
        g_settings.saveDir = directoryOf(path);
        saveSettings();
        setStatus(hwnd, st, L"已保存 " + baseName(path), false);
        if (g_settings.closeAfterSave) SetTimer(hwnd, TIMER_HIDE, 700, nullptr);
    } else {
        setStatus(hwnd, st, L"保存失败,请检查目录权限", true);
    }
}

void copyImage(HWND hwnd) {
    PopupState* st = getState(hwnd);
    if (!st) return;
    if (!st->hasSymbol) {
        setStatus(hwnd, st, L"请先输入要生成二维码的内容", true);
        return;
    }
    Gdiplus::Bitmap* bitmap = renderQrBitmap(st->symbol, S(g_settings.qrSize));
    if (!bitmap) {
        setStatus(hwnd, st, L"生成图片失败", true);
        return;
    }
    bool copied = copyBitmapToClipboard(hwnd, bitmap);
    delete bitmap;
    setStatus(hwnd, st, copied ? L"二维码图片已复制到剪贴板" : L"复制失败,剪贴板被占用", !copied);
}

void pasteFromClipboard(HWND hwnd) {
    PopupState* st = getState(hwnd);
    if (!st) return;
    std::wstring text = getClipboardText();
    if (text.empty()) {
        setStatus(hwnd, st, L"剪贴板中没有文本", true);
        return;
    }
    SetWindowTextW(st->edit, text.c_str());
    SendMessageW(st->edit, EM_SETSEL, 0, -1);
    SetFocus(st->edit);
    refreshQr(hwnd);
    setStatus(hwnd, st, L"已从剪贴板粘贴", false);
}

void hideWindow(HWND hwnd) {
    PopupState* st = getState(hwnd);
    if (st) {
        KillTimer(hwnd, TIMER_DEBOUNCE);
        KillTimer(hwnd, TIMER_STATUS);
        KillTimer(hwnd, TIMER_HIDE);
        st->message.clear();
        st->suppressDeactivate = false;
    }
    ShowWindow(hwnd, SW_HIDE);
}

LRESULT CALLBACK PopupProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    PopupState* st = getState(hwnd);

    switch (message) {
        case WM_CREATE: {
            st = new PopupState();
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)st);

            st->icon = (HICON)LoadImageW(g_hinst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, S(26),
                                         S(26), LR_DEFAULTCOLOR);
            st->editBrush = CreateSolidBrush(g_theme.fieldBg);

            st->edit = CreateWindowExW(
                0, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | ES_NOHIDESEL |
                    WS_VSCROLL,
                S(25), S(87), S(371), S(46), hwnd, (HMENU)(INT_PTR)IDC_EDIT, g_hinst, nullptr);
            SendMessageW(st->edit, WM_SETFONT, (WPARAM)g_fonts.body, TRUE);
            SendMessageW(st->edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                         MAKELPARAM(S(2), S(4)));

            struct {
                HWND* handle;
                int id;
                const wchar_t* text;
            } buttons[] = {
                {&st->btnSave, IDC_BTN_SAVE, L"保存图片"},
                {&st->btnCopy, IDC_BTN_COPY, L"复制图片"},
                {&st->btnPaste, IDC_BTN_PASTE, L"粘贴"},
                {&st->btnSettings, IDC_BTN_SETTINGS, L""},
                {&st->btnClose, IDC_BTN_CLOSE, L""},
            };
            for (auto& button : buttons) {
                *button.handle = CreateWindowExW(
                    0, L"BUTTON", button.text,
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 10, 10, hwnd,
                    (HMENU)(INT_PTR)button.id, g_hinst, nullptr);
                SendMessageW(*button.handle, WM_SETFONT, (WPARAM)g_fonts.bold, TRUE);
                installButtonSubclass(*button.handle);
            }

            layoutControls(hwnd, st);
            applyWindowShape(hwnd);
            return 0;
        }

        case WM_ACTIVATE: {
            if (LOWORD(wParam) == WA_INACTIVE && st && !st->suppressDeactivate &&
                g_settings.closeOnBlur) {
                hideWindow(hwnd);
            }
            break;
        }

        case WM_NCHITTEST: {
            LRESULT hit = DefWindowProcW(hwnd, message, wParam, lParam);
            if (hit != HTCLIENT) return hit;
            POINT local = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(hwnd, &local);
            RECT rc;
            GetClientRect(hwnd, &rc);
            const int grip = S(6);
            bool left = local.x < grip;
            bool right = local.x >= rc.right - grip;
            bool top = local.y < grip;
            bool bottom = local.y >= rc.bottom - grip;
            if (top && left) return HTTOPLEFT;
            if (top && right) return HTTOPRIGHT;
            if (bottom && left) return HTBOTTOMLEFT;
            if (bottom && right) return HTBOTTOMRIGHT;
            if (left) return HTLEFT;
            if (right) return HTRIGHT;
            if (top) return HTTOP;
            if (bottom) return HTBOTTOM;
            if (local.y < S(50)) return HTCAPTION;
            return HTCLIENT;
        }

        case WM_GETMINMAXINFO: {
            MINMAXINFO* info = (MINMAXINFO*)lParam;
            info->ptMinTrackSize.x = S(kMinWidth);
            info->ptMinTrackSize.y = S(kMinHeight);
            return 0;
        }

        case WM_SIZE: {
            if (st && wParam != SIZE_MINIMIZED) {
                layoutControls(hwnd, st);
                applyWindowShape(hwnd);
                RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
            }
            return 0;
        }

        case WM_ENTERSIZEMOVE:
            setPaintBuffered(true);
            return 0;

        case WM_EXITSIZEMOVE: {
            setPaintBuffered(false);
            InvalidateRect(hwnd, nullptr, FALSE);
            RECT rc;
            GetWindowRect(hwnd, &rc);
            int width = (int)((rc.right - rc.left) / g_scale + 0.5f);
            int height = (int)((rc.bottom - rc.top) / g_scale + 0.5f);
            if (width != g_settings.popupWidth || height != g_settings.popupHeight) {
                g_settings.popupWidth = width;
                g_settings.popupHeight = height;
                saveSettings();
            }
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            if (paintIsBuffered()) {
                RECT client;
                GetClientRect(hwnd, &client);
                PaintBuffer buffer = beginPaintBuffer(hwnd, client);
                paintPopup(hwnd, buffer.dc);
                endPaintBuffer(hwnd, buffer);
            } else {
                paintPopup(hwnd, dc);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_DRAWITEM: {
            const DRAWITEMSTRUCT* dis = (const DRAWITEMSTRUCT*)lParam;
            const BtnSpec* spec = findSpec((int)dis->CtlID);
            if (spec) drawButtonSpec(dis, *spec, false);
            return TRUE;
        }

        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC: {
            HDC dc = (HDC)wParam;
            SetTextColor(dc, g_theme.text);
            SetBkColor(dc, g_theme.fieldBg);
            if (st && st->editBrush) return (LRESULT)st->editBrush;
            return (LRESULT)GetStockObject(WHITE_BRUSH);
        }

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            int notification = HIWORD(wParam);

            if (id == IDC_EDIT && notification == EN_CHANGE) {
                SetTimer(hwnd, TIMER_DEBOUNCE, 110, nullptr);
                return 0;
            }
            switch (id) {
                case IDC_BTN_SAVE:
                    savePng(hwnd);
                    return 0;
                case IDC_BTN_COPY:
                    copyImage(hwnd);
                    return 0;
                case IDC_BTN_PASTE:
                    pasteFromClipboard(hwnd);
                    return 0;
                case IDC_BTN_SETTINGS:
                    if (st) st->suppressDeactivate = true;
                    hideWindow(hwnd);
                    showSettingsWindow();
                    return 0;
                case IDC_BTN_CLOSE:
                    hideWindow(hwnd);
                    return 0;
                default:
                    break;
            }
            break;
        }

        case WM_TIMER: {
            if (wParam == TIMER_DEBOUNCE) {
                KillTimer(hwnd, TIMER_DEBOUNCE);
                refreshQr(hwnd);
                return 0;
            }
            if (wParam == TIMER_STATUS) {
                KillTimer(hwnd, TIMER_STATUS);
                if (st) {
                    st->message.clear();
                    st->messageIsError = false;
                    InvalidateRect(hwnd, &st->layout.status, FALSE);
                }
                return 0;
            }
            if (wParam == TIMER_HIDE) {
                KillTimer(hwnd, TIMER_HIDE);
                hideWindow(hwnd);
                return 0;
            }
            break;
        }

        case WM_CLOSE:
            hideWindow(hwnd);
            return 0;

        case WM_SETTINGCHANGE:
            if (wParam == SPI_SETWORKAREA) {
                // Multi-monitor or taskbar changes: nothing cached, safe to ignore.
            }
            break;

        case WM_DESTROY: {
            if (st) {
                if (st->icon) DestroyIcon(st->icon);
                if (st->editBrush) DeleteObject(st->editBrush);
                delete st;
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            }
            g_popup = nullptr;
            return 0;
        }

        default:
            break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

}  // namespace

HWND createPopupWindow() {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {sizeof(wc)};
        wc.lpfnWndProc = PopupProc;
        wc.hInstance = g_hinst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kWndClassPopup;
        wc.style = CS_DROPSHADOW;
        wc.hIcon = (HICON)LoadImageW(g_hinst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, 32, 32, 0);
        RegisterClassExW(&wc);
        registered = true;
    }

    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kWndClassPopup, kAppName,
                                WS_POPUP | WS_CLIPCHILDREN, 0, 0, S(g_settings.popupWidth), S(g_settings.popupHeight),
                                nullptr, nullptr, g_hinst,
                                nullptr);
    g_popup = hwnd;
    return hwnd;
}

void showPopup() {
    if (g_popup && IsWindowVisible(g_popup) && GetForegroundWindow() == g_popup) {
        hidePopup();
        return;
    }
    if (!g_popup) createPopupWindow();
    if (!g_popup) return;

    PopupState* st = getState(g_popup);
    if (!st) return;

    POINT cursor;
    GetCursorPos(&cursor);
    MONITORINFO monitor = {sizeof(monitor)};
    GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &monitor);

    int width = S(g_settings.popupWidth);
    int height = S(g_settings.popupHeight);
    int x = cursor.x - width / 2;
    int y = cursor.y + S(18);
    if (y + height > monitor.rcWork.bottom) y = cursor.y - height - S(18);
    if (y < monitor.rcWork.top + S(8)) y = monitor.rcWork.top + S(8);
    int maxX = monitor.rcWork.right - width - S(8);
    if (x > maxX) x = maxX;
    if (x < monitor.rcWork.left + S(8)) x = monitor.rcWork.left + S(8);

    SetWindowPos(g_popup, HWND_TOPMOST, x, y, width, height, SWP_SHOWWINDOW);
    ShowWindow(g_popup, SW_SHOW);
    SetForegroundWindow(g_popup);
    SetFocus(st->edit);

    if (g_settings.autoPaste) {
        std::wstring text = getClipboardText();
        if (!text.empty()) {
            std::wstring current = getEditText(st->edit);
            if (text != current) SetWindowTextW(st->edit, text.c_str());
        }
    }
    SendMessageW(st->edit, EM_SETSEL, 0, -1);
    refreshQr(g_popup);
    InvalidateRect(g_popup, nullptr, FALSE);
}

void hidePopup() {
    if (g_popup) hideWindow(g_popup);
}

void refreshPopupContent(bool pasteFirst) {
    if (!g_popup) return;
    if (pasteFirst) {
        pasteFromClipboard(g_popup);
        return;
    }
    refreshQr(g_popup);
}

void popupSetStatus(const std::wstring& text, bool isError) {
    if (!g_popup) return;
    PopupState* st = getState(g_popup);
    if (st) setStatus(g_popup, st, text, isError);
}
