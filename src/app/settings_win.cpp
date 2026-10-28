#include "app.h"

#include "text_util.h"

#include <commctrl.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <windowsx.h>

#include <algorithm>
#include <string>
#include <vector>

#include "resource.h"

namespace {

constexpr int IDC_HOTKEY = 3001;
constexpr int IDC_BTN_RESET_HOTKEY = 3002;

constexpr int IDC_TOGGLE_BLUR = 3011;
constexpr int IDC_TOGGLE_AUTOPASTE = 3012;
constexpr int IDC_TOGGLE_CLOSESAVE = 3013;
constexpr int IDC_TOGGLE_AUTOSTART = 3014;

constexpr int IDC_ECC_L = 3021;
constexpr int IDC_ECC_M = 3022;
constexpr int IDC_ECC_Q = 3023;
constexpr int IDC_ECC_H = 3024;

constexpr int IDC_SIZE_EDIT = 3031;
constexpr int IDC_SIZE_MINUS = 3032;
constexpr int IDC_SIZE_PLUS = 3033;

constexpr int IDC_DIR_EDIT = 3041;
constexpr int IDC_BTN_BROWSE = 3042;

constexpr int IDC_STYLE_WIN32 = 3051;
constexpr int IDC_STYLE_WINUI = 3052;

constexpr int IDC_MODE_SYSTEM = 3071;
constexpr int IDC_MODE_LIGHT = 3072;
constexpr int IDC_MODE_DARK = 3073;

constexpr int IDC_BTN_SAVE_RESTART = 3065;

constexpr int IDC_BTN_DEFAULTS = 3061;
constexpr int IDC_BTN_CANCEL = ID_SETTINGS_CANCEL;
constexpr int IDC_BTN_SAVE = ID_SETTINGS_SAVE;
constexpr int IDC_BTN_CLOSE = 3064;

constexpr int kWidth = 560;
constexpr int kHeight = 688;

const BtnSpec kSpecs[] = {
    {IDC_BTN_RESET_HOTKEY, BtnKind::Secondary, L"重置"},
    {IDC_TOGGLE_BLUR, BtnKind::Toggle, L""},
    {IDC_TOGGLE_AUTOPASTE, BtnKind::Toggle, L""},
    {IDC_TOGGLE_CLOSESAVE, BtnKind::Toggle, L""},
    {IDC_TOGGLE_AUTOSTART, BtnKind::Toggle, L""},
    {IDC_ECC_L, BtnKind::Chip, L"低  7%"},
    {IDC_ECC_M, BtnKind::Chip, L"中  15%"},
    {IDC_ECC_Q, BtnKind::Chip, L"高  25%"},
    {IDC_ECC_H, BtnKind::Chip, L"最高 30%"},
    {IDC_SIZE_MINUS, BtnKind::Secondary, L"−"},
    {IDC_SIZE_PLUS, BtnKind::Secondary, L"+"},
    {IDC_BTN_BROWSE, BtnKind::Secondary, L"浏览…"},
    {IDC_STYLE_WIN32, BtnKind::Chip, L"Win32 经典"},
    {IDC_STYLE_WINUI, BtnKind::Chip, L"WinUI 3"},
    {IDC_MODE_SYSTEM, BtnKind::Chip, L"跟随系统"},
    {IDC_MODE_LIGHT, BtnKind::Chip, L"浅色"},
    {IDC_MODE_DARK, BtnKind::Chip, L"深色"},
    {IDC_BTN_DEFAULTS, BtnKind::Secondary, L"恢复默认"},
    {IDC_BTN_CANCEL, BtnKind::Secondary, L"取消"},
    {IDC_BTN_SAVE, BtnKind::Primary, L"保存"},
    {IDC_BTN_SAVE_RESTART, BtnKind::Secondary, L"保存并重启"},
    {IDC_BTN_CLOSE, BtnKind::IconClose, L""},
};

struct Rect2 {
    RECT hotkey;
    RECT btnResetHotkey;
    RECT toggles[4];
    RECT ecc[4];
    RECT sizeField;
    RECT sizeEdit;
    RECT sizeMinus;
    RECT sizePlus;
    RECT dirField;
    RECT dirEdit;
    RECT btnBrowse;
    RECT style[2];
    RECT mode[3];
    RECT btnDefaults;
    RECT btnCancel;
    RECT btnSave;
    RECT btnSaveRestart;
    RECT btnClose;
};

struct SettingsState {
    HWND hotkey = nullptr;
    HWND btnResetHotkey = nullptr;
    HWND toggles[4] = {};
    HWND eccChips[4] = {};
    HWND sizeEdit = nullptr;
    HWND sizeMinus = nullptr;
    HWND sizePlus = nullptr;
    HWND dirEdit = nullptr;
    HWND btnBrowse = nullptr;
    HWND styleChips[2] = {};
    HWND modeChips[3] = {};
    HWND btnDefaults = nullptr;
    HWND btnCancel = nullptr;
    HWND btnSave = nullptr;
    HWND btnSaveRestart = nullptr;
    HWND btnClose = nullptr;

    bool toggleValues[4] = {true, true, true, false};
    int ecc = 1;
    int style = 1;
    int themeMode = 0;
    int qrSize = 360;
    UINT mods = MOD_CONTROL | MOD_ALT;
    UINT vk = 'Q';
    std::wstring dir;
    HBRUSH editBrush = nullptr;
    Rect2 rects;
};

SettingsState* getState(HWND hwnd) {
    return reinterpret_cast<SettingsState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

const BtnSpec* findSpec(int id) {
    for (const BtnSpec& spec : kSpecs) {
        if (spec.id == id) return &spec;
    }
    return nullptr;
}

int chipIndexForId(int id, const int* ids, int count) {
    for (int i = 0; i < count; ++i)
        if (ids[i] == id) return i;
    return -1;
}

// ------------------------------------------------------------------ hotkey capture control

UINT g_captureMods = MOD_CONTROL | MOD_ALT;
UINT g_captureVk = 'Q';
bool g_captureFocused = false;

bool isPureModifier(UINT vk) {
    switch (vk) {
        case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
        case VK_MENU: case VK_LMENU: case VK_RMENU:
        case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:
        case VK_LWIN: case VK_RWIN:
            return true;
        default:
            return false;
    }
}

UINT currentModifierState() {
    UINT mods = 0;
    if (GetKeyState(VK_CONTROL) & 0x8000) mods |= MOD_CONTROL;
    if (GetKeyState(VK_MENU) & 0x8000) mods |= MOD_ALT;
    if (GetKeyState(VK_SHIFT) & 0x8000) mods |= MOD_SHIFT;
    if ((GetKeyState(VK_LWIN) & 0x8000) || (GetKeyState(VK_RWIN) & 0x8000)) mods |= MOD_WIN;
    return mods;
}

bool isFunctionKey(UINT vk) {
    return vk >= VK_F1 && vk <= VK_F24;
}

void paintHotkeyControl(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT client;
    GetClientRect(hwnd, &client);

    PaintBuffer buffer = beginPaintBuffer(hwnd, client);

    COLORREF border = g_captureFocused ? g_theme.accent : g_theme.fieldBorder;
    drawRoundRect(buffer.dc, client, g_theme.radius, g_theme.fieldBg, border,
                  g_captureFocused ? 2 : 1);

    std::wstring text = g_captureVk ? formatHotkey(g_captureMods, g_captureVk)
                                    : L"点击后按下组合键";
    RECT textRect = client;
    textRect.left += S(10);
    textRect.right -= S(10);
    drawTextGdi(buffer.dc, g_fonts.bold, g_captureVk ? g_theme.text : g_theme.textDim, text.c_str(),
                textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    endPaintBuffer(hwnd, buffer);
    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK HotkeyProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_LBUTTONDOWN:
            SetFocus(hwnd);
            return 0;

        case WM_SETFOCUS:
            g_captureFocused = true;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;

        case WM_KILLFOCUS:
            g_captureFocused = false;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            UINT vk = (UINT)wParam;
            if (vk == VK_TAB || vk == VK_ESCAPE) break;  // let the dialog manager handle these
            if (isPureModifier(vk)) break;

            UINT mods = currentModifierState();
            if (mods == 0 && !isFunctionKey(vk)) {
                MessageBeep(MB_ICONERROR);
                break;
            }
            g_captureMods = mods;
            g_captureVk = vk;
            InvalidateRect(hwnd, nullptr, FALSE);
            PostMessageW(GetParent(hwnd), WM_APP_HOTKEY_CHANGED, 0, 0);
            return 0;
        }

        case WM_PAINT:
            paintHotkeyControl(hwnd);
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_CHAR:
            return 0;

        default:
            break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

// ------------------------------------------------------------------ helpers

void setToggle(SettingsState* st, int index, bool value) {
    st->toggleValues[index] = value;
    InvalidateRect(st->toggles[index], nullptr, FALSE);
}

void setChip(HWND hwnd, SettingsState* st, int index) {
    st->ecc = index;
    for (int i = 0; i < 4; ++i) InvalidateRect(st->eccChips[i], nullptr, FALSE);
    (void)hwnd;
}

void setStyleChip(SettingsState* st, int index) {
    st->style = index;
    for (int i = 0; i < 2; ++i) InvalidateRect(st->styleChips[i], nullptr, FALSE);
    for (int i = 0; i < 3; ++i) InvalidateRect(st->modeChips[i], nullptr, FALSE);
}

void browseForFolder(HWND hwnd, SettingsState* st) {
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dialog))))
        return;
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    if (SUCCEEDED(dialog->Show(hwnd))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                SetWindowTextW(st->dirEdit, path);
                st->dir = path;
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dialog->Release();
}

void saveSettingsFromDialog(HWND hwnd, bool restartAfterSave) {
    SettingsState* st = getState(hwnd);
    if (!st) return;

    if (!st->vk) {
        MessageBoxW(hwnd, L"请先设置一个快捷键组合。", kAppName, MB_ICONWARNING | MB_OK);
        return;
    }

    UINT previousMods = g_settings.hotkeyMods;
    UINT previousVk = g_settings.hotkeyVk;
    if (st->mods != previousMods || st->vk != previousVk) {
        if (!registerAppHotkey(st->mods, st->vk)) {
            g_settings.hotkeyMods = previousMods;
            g_settings.hotkeyVk = previousVk;
            registerAppHotkey(previousMods, previousVk);
            std::wstring message = L"快捷键 " + formatHotkey(st->mods, st->vk) +
                                   L" 注册失败,可能已被其他程序占用。\n请换一个组合键。";
            MessageBoxW(hwnd, message.c_str(), kAppName, MB_ICONWARNING | MB_OK);
            return;
        }
    }

    wchar_t sizeText[32] = {0};
    GetWindowTextW(st->sizeEdit, sizeText, ARRAYSIZE(sizeText));
    int size = tu::toInt(sizeText);
    size = std::max(160, std::min(1200, size));
    wchar_t directory[MAX_PATH * 2] = {0};
    GetWindowTextW(st->dirEdit, directory, ARRAYSIZE(directory));

    g_settings.hotkeyMods = st->mods;
    g_settings.hotkeyVk = st->vk;
    g_settings.closeOnBlur = st->toggleValues[0];
    g_settings.autoPaste = st->toggleValues[1];
    g_settings.closeAfterSave = st->toggleValues[2];
    g_settings.eccLevel = st->ecc;
    g_settings.uiStyle = st->style;
    g_settings.themeMode = st->themeMode;
    g_settings.qrSize = size;
    g_settings.saveDir = directory;
    ensureDirectory(g_settings.saveDir);
    saveSettings();
    applyAutostart(st->toggleValues[3]);

    int style = st->style;
    HWND popup = g_popup;
    g_popup = nullptr;
    DestroyWindow(hwnd);
    if (popup) DestroyWindow(popup);
    applyTheme(style);
    rebuildTrayIcon();
    if (restartAfterSave) restartApplication();
}

void paintSettings(HWND hwnd, HDC hdc, const RECT& dirty) {
    SettingsState* st = getState(hwnd);
    if (!st) return;

    RECT client;
    GetClientRect(hwnd, &client);
    paintWindowBackdrop(hdc, client, true);
    if (!rectsOverlap(dirty, client)) return;

    const int clientWidth = client.right;
    const int clientHeight = client.bottom;

    RECT titleRect = {S(16), S(8), clientWidth - S(64), S(36)};
    drawTextGdi(hdc, g_fonts.title, g_theme.text, L"设置", titleRect,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    {
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
    }

    auto label = [&](const wchar_t* text, int x, int y, int w, int h, HFONT font, COLORREF color,
                     UINT flags) {
        RECT r = {S(x), S(y), S(x + w), S(y + h)};
        drawTextGdi(hdc, font, color, text, r, flags);
    };

    label(L"快捷键", 16, 52, 200, 20, g_fonts.bold, g_theme.text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    label(L"点击后按下新的组合键,例如 Ctrl + Alt + Q", 16, 110, 520, 18, g_fonts.hint,
          g_theme.textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    label(L"行为", 16, 134, 200, 20, g_fonts.bold, g_theme.text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    const wchar_t* toggleLabels[4] = {L"点击窗口外自动关闭", L"打开时自动粘贴剪贴板",
                                      L"保存后关闭窗口", L"开机自启动"};
    for (int i = 0; i < 4; ++i) {
        int y = 154 + i * 36;
        label(toggleLabels[i], 16, y, 420, 26, g_fonts.body, g_theme.text,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    label(L"二维码", 16, 290, 200, 20, g_fonts.bold, g_theme.text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    label(L"纠错级别", 16, 312, 200, 18, g_fonts.hint, g_theme.textDim,
          DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    label(L"图片尺寸", 16, 366, 120, 18, g_fonts.hint, g_theme.textDim,
          DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    label(L"像素,范围 160 - 1200", 186, 388, 250, 30, g_fonts.hint, g_theme.textDim,
          DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    label(L"保存目录", 16, 424, 120, 18, g_fonts.hint, g_theme.textDim,
          DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    label(L"界面风格", 16, 482, 200, 18, g_fonts.hint, g_theme.textDim,
          DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    label(L"颜色模式", 16, 540, 200, 18, g_fonts.hint, g_theme.textDim,
          DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    label(L"Win32 走系统主题引擎;WinUI 3 用系统主色与官方图标字体;点“保存并重启”立即生效。",
          16, 594, clientWidth - S(180), 18, g_fonts.hint, g_theme.textDim,
          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    {
        // clientWidth is already in physical pixels, so draw it directly instead of via label().
        RECT versionRect = {clientWidth - S(160), S(594), clientWidth - S(16), S(612)};
        std::wstring version = std::wstring(L"v") + kAppVersion;
        drawTextGdi(hdc, g_fonts.hint, g_theme.textDim, version.c_str(), versionRect,
                    DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }

    // Field frames: themed by the OS in classic mode, Fluent surfaces otherwise.
    paintFieldFrame(hwnd, hdc, st->rects.sizeField, g_theme.radius);
    paintFieldFrame(hwnd, hdc, st->rects.dirField, g_theme.radius);
}

void layoutSettings(HWND hwnd, SettingsState* st) {
    Rect2& r = st->rects;
    auto place = [&](HWND control, const RECT& rc) {
        if (control)
            SetWindowPos(control, nullptr, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
    };

    RECT client = {0, 0, S(kWidth), S(kHeight)};
    GetClientRect(hwnd, &client);
    const int clientWidth = client.right;
    const int clientHeight = client.bottom;
    const int extraHeight = clientHeight - S(kHeight);

    r.hotkey = {S(16), S(74), S(256), S(108)};
    r.btnResetHotkey = {S(264), S(74), S(340), S(108)};

    for (int i = 0; i < 4; ++i) {
        int y = S(154 + i * 36);
        r.toggles[i] = {clientWidth - S(60), y, clientWidth - S(16), y + S(26)};
    }

    for (int i = 0; i < 4; ++i) {
        int x = S(16 + i * 88);
        r.ecc[i] = {x, S(332), x + S(80), S(362)};
    }

    r.sizeField = {S(16), S(388), S(96), S(418)};
    r.sizeEdit = {S(23), S(393), S(90), S(414)};
    r.sizeMinus = {S(104), S(388), S(138), S(418)};
    r.sizePlus = {S(142), S(388), S(176), S(418)};

    r.dirField = {S(16), S(444), clientWidth - S(108), S(474)};
    r.dirEdit = {S(25), S(450), clientWidth - S(118), S(470)};
    r.btnBrowse = {clientWidth - S(100), S(444), clientWidth - S(16), S(474)};

    for (int i = 0; i < 2; ++i) {
        int x = S(16 + i * 128);
        r.style[i] = {x, S(502), x + S(120), S(532)};
    }
    for (int i = 0; i < 3; ++i) {
        int x = S(16 + i * 108);
        r.mode[i] = {x, S(558), x + S(100), S(588)};
    }

    r.btnDefaults = {S(16), S(636) + extraHeight, S(116), S(672) + extraHeight};
    r.btnCancel = {S(220), S(636) + extraHeight, S(308), S(672) + extraHeight};
    r.btnSave = {S(316), S(636) + extraHeight, S(404), S(672) + extraHeight};
    r.btnSaveRestart = {clientWidth - S(148), S(636) + extraHeight, clientWidth - S(16),
                        S(672) + extraHeight};
    r.btnClose = {clientWidth - S(46), S(10), clientWidth - S(16), S(40)};

    place(st->hotkey, r.hotkey);
    place(st->btnResetHotkey, r.btnResetHotkey);
    for (int i = 0; i < 4; ++i) place(st->toggles[i], r.toggles[i]);
    for (int i = 0; i < 4; ++i) place(st->eccChips[i], r.ecc[i]);
    place(st->sizeEdit, r.sizeEdit);
    place(st->sizeMinus, r.sizeMinus);
    place(st->sizePlus, r.sizePlus);
    place(st->dirEdit, r.dirEdit);
    place(st->btnBrowse, r.btnBrowse);
    for (int i = 0; i < 2; ++i) place(st->styleChips[i], r.style[i]);
    for (int i = 0; i < 3; ++i) place(st->modeChips[i], r.mode[i]);
    place(st->btnDefaults, r.btnDefaults);
    place(st->btnCancel, r.btnCancel);
    place(st->btnSave, r.btnSave);
    place(st->btnSaveRestart, r.btnSaveRestart);
    place(st->btnClose, r.btnClose);
}

void createChildControls(HWND hwnd, SettingsState* st) {
    st->editBrush = CreateSolidBrush(g_theme.fieldBg);

    st->hotkey = CreateWindowExW(0, kWndClassHotkey, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0,
                                 10, 10, hwnd, (HMENU)(INT_PTR)IDC_HOTKEY, g_hinst, nullptr);

    struct {
        HWND* target;
        int id;
        const wchar_t* text;
    } buttons[] = {
        {&st->btnResetHotkey, IDC_BTN_RESET_HOTKEY, L"重置"},
        {&st->toggles[0], IDC_TOGGLE_BLUR, L""},
        {&st->toggles[1], IDC_TOGGLE_AUTOPASTE, L""},
        {&st->toggles[2], IDC_TOGGLE_CLOSESAVE, L""},
        {&st->toggles[3], IDC_TOGGLE_AUTOSTART, L""},
        {&st->eccChips[0], IDC_ECC_L, L"低  7%"},
        {&st->eccChips[1], IDC_ECC_M, L"中  15%"},
        {&st->eccChips[2], IDC_ECC_Q, L"高  25%"},
        {&st->eccChips[3], IDC_ECC_H, L"最高 30%"},
        {&st->sizeMinus, IDC_SIZE_MINUS, L"−"},
        {&st->sizePlus, IDC_SIZE_PLUS, L"+"},
        {&st->btnBrowse, IDC_BTN_BROWSE, L"浏览…"},
        {&st->styleChips[0], IDC_STYLE_WIN32, L"Win32 经典"},
        {&st->styleChips[1], IDC_STYLE_WINUI, L"WinUI 3"},
        {&st->modeChips[0], IDC_MODE_SYSTEM, L"跟随系统"},
        {&st->modeChips[1], IDC_MODE_LIGHT, L"浅色"},
        {&st->modeChips[2], IDC_MODE_DARK, L"深色"},
        {&st->btnDefaults, IDC_BTN_DEFAULTS, L"恢复默认"},
        {&st->btnCancel, IDC_BTN_CANCEL, L"取消"},
        {&st->btnSave, IDC_BTN_SAVE, L"保存"},
        {&st->btnSaveRestart, IDC_BTN_SAVE_RESTART, L"保存并重启"},
        {&st->btnClose, IDC_BTN_CLOSE, L""},
    };

    for (auto& button : buttons) {
        *button.target = CreateWindowExW(0, L"BUTTON", button.text,
                                         WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0,
                                         10, 10, hwnd, (HMENU)(INT_PTR)button.id, g_hinst, nullptr);
        SendMessageW(*button.target, WM_SETFONT, (WPARAM)g_fonts.bold, TRUE);
        installButtonSubclass(*button.target);
    }

    st->sizeEdit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER |
                                                        ES_CENTER | ES_AUTOHSCROLL,
                                   0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)IDC_SIZE_EDIT, g_hinst,
                                   nullptr);
    SendMessageW(st->sizeEdit, WM_SETFONT, (WPARAM)g_fonts.body, TRUE);
    SendMessageW(st->sizeEdit, EM_SETLIMITTEXT, 4, 0);

    st->dirEdit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                                        ES_AUTOHSCROLL,
                                  0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)IDC_DIR_EDIT, g_hinst,
                                  nullptr);
    SendMessageW(st->dirEdit, WM_SETFONT, (WPARAM)g_fonts.body, TRUE);

    wchar_t sizeText[16] = {0};
    wsprintfW(sizeText, L"%d", st->qrSize);
    SetWindowTextW(st->sizeEdit, sizeText);
    SetWindowTextW(st->dirEdit, st->dir.c_str());
}

void loadValues(HWND hwnd, SettingsState* st) {
    st->mods = g_settings.hotkeyMods;
    st->vk = g_settings.hotkeyVk;
    g_captureMods = st->mods;
    g_captureVk = st->vk;

    st->toggleValues[0] = g_settings.closeOnBlur;
    st->toggleValues[1] = g_settings.autoPaste;
    st->toggleValues[2] = g_settings.closeAfterSave;
    st->toggleValues[3] = g_settings.autostart;
    st->ecc = g_settings.eccLevel;
    st->style = g_settings.uiStyle;
    st->themeMode = g_settings.themeMode;
    st->qrSize = g_settings.qrSize;
    st->dir = g_settings.saveDir;

    for (int i = 0; i < 4; ++i) InvalidateRect(st->toggles[i], nullptr, FALSE);
    for (int i = 0; i < 4; ++i) InvalidateRect(st->eccChips[i], nullptr, FALSE);
    for (int i = 0; i < 2; ++i) InvalidateRect(st->styleChips[i], nullptr, FALSE);
    InvalidateRect(st->hotkey, nullptr, FALSE);
    (void)hwnd;
}

LRESULT CALLBACK SettingsProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    SettingsState* st = getState(hwnd);

    switch (message) {
        case WM_CREATE: {
            st = new SettingsState();
            st->ecc = g_settings.eccLevel;
            st->style = g_settings.uiStyle;
            st->qrSize = g_settings.qrSize;
            st->dir = g_settings.saveDir;
            st->mods = g_settings.hotkeyMods;
            st->vk = g_settings.hotkeyVk;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)st);

            createChildControls(hwnd, st);
            layoutSettings(hwnd, st);
            loadValues(hwnd, st);

            applyWindowShape(hwnd);
            return 0;
        }

        case WM_APP_HOTKEY_CHANGED: {
            if (st) {
                st->mods = g_captureMods;
                st->vk = g_captureVk;
            }
            return 0;
        }

        case WM_NCHITTEST: {
            LRESULT hit = DefWindowProcW(hwnd, message, wParam, lParam);
            if (hit == HTCLIENT) {
                POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                ScreenToClient(hwnd, &pt);
                if (pt.y < S(48)) return HTCAPTION;
            }
            return hit;
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
                paintSettings(hwnd, buffer.dc, ps.rcPaint);
                endPaintBuffer(hwnd, buffer);
            } else {
                paintSettings(hwnd, dc, ps.rcPaint);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_DRAWITEM: {
            const DRAWITEMSTRUCT* dis = (const DRAWITEMSTRUCT*)lParam;
            const BtnSpec* spec = findSpec((int)dis->CtlID);
            if (spec && st) {
                bool checkedState = false;
                switch (dis->CtlID) {
                    case IDC_TOGGLE_BLUR: checkedState = st->toggleValues[0]; break;
                    case IDC_TOGGLE_AUTOPASTE: checkedState = st->toggleValues[1]; break;
                    case IDC_TOGGLE_CLOSESAVE: checkedState = st->toggleValues[2]; break;
                    case IDC_TOGGLE_AUTOSTART: checkedState = st->toggleValues[3]; break;
                    case IDC_ECC_L: checkedState = st->ecc == 0; break;
                    case IDC_ECC_M: checkedState = st->ecc == 1; break;
                    case IDC_ECC_Q: checkedState = st->ecc == 2; break;
                    case IDC_ECC_H: checkedState = st->ecc == 3; break;
                    case IDC_STYLE_WIN32: checkedState = st->style == 0; break;
                    case IDC_STYLE_WINUI: checkedState = st->style == 1; break;
                    case IDC_MODE_SYSTEM: checkedState = st->themeMode == 0; break;
                    case IDC_MODE_LIGHT: checkedState = st->themeMode == 1; break;
                    case IDC_MODE_DARK: checkedState = st->themeMode == 2; break;
                    default: break;
                }
                drawButtonSpec(dis, *spec, checkedState);
            }
            return TRUE;
        }

        case WM_ENTERSIZEMOVE:
            setPaintBuffered(true);
            return 0;

        case WM_EXITSIZEMOVE:
            setPaintBuffered(false);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;

        case WM_SIZE: {
            if (st && wParam != SIZE_MINIMIZED) {
                layoutSettings(hwnd, st);
                applyWindowShape(hwnd);
                RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
            }
            return 0;
        }

        case WM_GETMINMAXINFO: {
            MINMAXINFO* info = (MINMAXINFO*)lParam;
            info->ptMinTrackSize.x = S(kWidth);
            info->ptMinTrackSize.y = S(kHeight);
            return 0;
        }

        case WM_CTLCOLOREDIT: {
            HDC dc = (HDC)wParam;
            SetTextColor(dc, g_theme.text);
            SetBkColor(dc, g_theme.fieldBg);
            if (st && st->editBrush) return (LRESULT)st->editBrush;
            return (LRESULT)GetStockObject(WHITE_BRUSH);
        }

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            int notification = HIWORD(wParam);
            if (!st) break;

            if (id == IDC_SIZE_EDIT && notification == EN_CHANGE) {
                wchar_t text[16] = {0};
                GetWindowTextW(st->sizeEdit, text, ARRAYSIZE(text));
                int value = tu::toInt(text);
                if (value >= 160 && value <= 1200) st->qrSize = value;
                return 0;
            }

            const int eccIds[4] = {IDC_ECC_L, IDC_ECC_M, IDC_ECC_Q, IDC_ECC_H};
            const int styleIds[2] = {IDC_STYLE_WIN32, IDC_STYLE_WINUI};
            const int modeIds[3] = {IDC_MODE_SYSTEM, IDC_MODE_LIGHT, IDC_MODE_DARK};

            int eccIndex = chipIndexForId(id, eccIds, 4);
            if (eccIndex >= 0) {
                setChip(hwnd, st, eccIndex);
                return 0;
            }
            int styleIndex = chipIndexForId(id, styleIds, 2);
            if (styleIndex >= 0) {
                setStyleChip(st, styleIndex);
                return 0;
            }
            int modeIndex = chipIndexForId(id, modeIds, 3);
            if (modeIndex >= 0) {
                st->themeMode = modeIndex;
                for (int i = 0; i < 3; ++i) InvalidateRect(st->modeChips[i], nullptr, FALSE);
                return 0;
            }

            switch (id) {
                case IDC_TOGGLE_BLUR: setToggle(st, 0, !st->toggleValues[0]); return 0;
                case IDC_TOGGLE_AUTOPASTE: setToggle(st, 1, !st->toggleValues[1]); return 0;
                case IDC_TOGGLE_CLOSESAVE: setToggle(st, 2, !st->toggleValues[2]); return 0;
                case IDC_TOGGLE_AUTOSTART: setToggle(st, 3, !st->toggleValues[3]); return 0;
                case IDC_BTN_RESET_HOTKEY:
                    g_captureMods = MOD_CONTROL | MOD_ALT;
                    g_captureVk = 'Q';
                    st->mods = g_captureMods;
                    st->vk = g_captureVk;
                    InvalidateRect(st->hotkey, nullptr, FALSE);
                    return 0;
                case IDC_SIZE_MINUS: {
                    st->qrSize = std::max(160, st->qrSize - 40);
                    wchar_t text[16] = {0};
                    wsprintfW(text, L"%d", st->qrSize);
                    SetWindowTextW(st->sizeEdit, text);
                    return 0;
                }
                case IDC_SIZE_PLUS: {
                    st->qrSize = std::min(1200, st->qrSize + 40);
                    wchar_t text[16] = {0};
                    wsprintfW(text, L"%d", st->qrSize);
                    SetWindowTextW(st->sizeEdit, text);
                    return 0;
                }
                case IDC_BTN_BROWSE: browseForFolder(hwnd, st); return 0;
                case IDC_BTN_DEFAULTS: {
                    for (int i = 0; i < 4; ++i) st->toggleValues[i] = (i != 3);
                    st->toggleValues[3] = false;
                    st->ecc = 1;
                    st->style = 1;
                    st->themeMode = 0;
                    st->qrSize = 360;
                    st->dir = defaultSaveDir();
                    g_captureMods = MOD_CONTROL | MOD_ALT;
                    g_captureVk = 'Q';
                    st->mods = g_captureMods;
                    st->vk = g_captureVk;
                    SetWindowTextW(st->sizeEdit, L"360");
                    SetWindowTextW(st->dirEdit, st->dir.c_str());
                    for (int i = 0; i < 4; ++i) InvalidateRect(st->toggles[i], nullptr, FALSE);
                    for (int i = 0; i < 4; ++i) InvalidateRect(st->eccChips[i], nullptr, FALSE);
                    for (int i = 0; i < 2; ++i) InvalidateRect(st->styleChips[i], nullptr, FALSE);
                    for (int i = 0; i < 3; ++i) InvalidateRect(st->modeChips[i], nullptr, FALSE);
                    InvalidateRect(st->hotkey, nullptr, FALSE);
                    return 0;
                }
                case IDC_BTN_SAVE: saveSettingsFromDialog(hwnd, false); return 0;
                case IDC_BTN_SAVE_RESTART: saveSettingsFromDialog(hwnd, true); return 0;
                case IDC_BTN_CANCEL:
                case IDC_BTN_CLOSE: DestroyWindow(hwnd); return 0;
                default: break;
            }
            break;
        }

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY: {
            g_settingsOpen = false;
            g_settingsWnd = nullptr;
            if (st) {
                if (st->editBrush) DeleteObject(st->editBrush);
                delete st;
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            }
            return 0;
        }

        default:
            break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

}  // namespace

HWND createSettingsWindow() {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW hotkeyClass = {sizeof(hotkeyClass)};
        hotkeyClass.lpfnWndProc = HotkeyProc;
        hotkeyClass.hInstance = g_hinst;
        hotkeyClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        hotkeyClass.lpszClassName = kWndClassHotkey;
        RegisterClassExW(&hotkeyClass);

        WNDCLASSEXW wc = {sizeof(wc)};
        wc.lpfnWndProc = SettingsProc;
        wc.hInstance = g_hinst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kWndClassSettings;
        wc.style = CS_DROPSHADOW;
        wc.hIcon = (HICON)LoadImageW(g_hinst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, 32, 32, 0);
        RegisterClassExW(&wc);
        registered = true;
    }

    ensureFonts();
    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, kWndClassSettings, L"设置",
                                WS_POPUP | WS_CLIPCHILDREN, 0, 0,
                                S(kWidth), S(kHeight), nullptr, nullptr, g_hinst, nullptr);
    g_settingsWnd = hwnd;
    g_settingsOpen = hwnd != nullptr;
    return hwnd;
}

void showSettingsWindow() {
    if (g_settingsWnd && IsWindow(g_settingsWnd)) {
        ShowWindow(g_settingsWnd, SW_SHOW);
        SetForegroundWindow(g_settingsWnd);
        return;
    }

    if (!createSettingsWindow()) return;

    RECT workArea;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    int width = S(kWidth), height = S(kHeight);
    int x = workArea.left + ((workArea.right - workArea.left) - width) / 2;
    int y = workArea.top + ((workArea.bottom - workArea.top) - height) / 2;
    if (y < workArea.top + S(8)) y = workArea.top + S(8);

    SetWindowPos(g_settingsWnd, HWND_TOP, x, y, width, height, SWP_SHOWWINDOW);
    ShowWindow(g_settingsWnd, SW_SHOW);
    SetForegroundWindow(g_settingsWnd);
}

void settingsChangedExternally() {
    if (g_settingsWnd && IsWindow(g_settingsWnd)) {
        SettingsState* st = getState(g_settingsWnd);
        if (st) loadValues(g_settingsWnd, st);
        InvalidateRect(g_settingsWnd, nullptr, TRUE);
    }
}
