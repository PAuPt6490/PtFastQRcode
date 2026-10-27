#include "app.h"

#include <shellapi.h>
#include <shlwapi.h>

#include <algorithm>
#include <cstdio>
#include <string>

#include "resource.h"

namespace {

constexpr int kTrayId = 1;
constexpr int kHotkeyIdValue = 1;
constexpr UINT kFirstShowTimer = 10;

constexpr UINT IDM_SHOW = 4001;
constexpr UINT IDM_SETTINGS = 4002;
constexpr UINT IDM_CLICKOUTSIDE = 4003;
constexpr UINT IDM_AUTOSTART = 4004;
constexpr UINT IDM_ABOUT = 4005;
constexpr UINT IDM_EXIT = 4006;

NOTIFYICONDATAW g_tray = {};
HICON g_trayIcon = nullptr;
UINT g_taskbarCreatedMessage = 0;
HACCEL g_popupAccel = nullptr;
HACCEL g_settingsAccel = nullptr;
bool g_trayVisible = false;

std::wstring keyName(UINT vk) {
    if (vk >= 'A' && vk <= 'Z') return std::wstring(1, (wchar_t)vk);
    if (vk >= '0' && vk <= '9') return std::wstring(1, (wchar_t)vk);
    if (vk >= VK_F1 && vk <= VK_F24) {
        wchar_t buffer[8];
        wsprintfW(buffer, L"F%d", (int)(vk - VK_F1 + 1));
        return buffer;
    }
    switch (vk) {
        case VK_SPACE: return L"Space";
        case VK_TAB: return L"Tab";
        case VK_RETURN: return L"Enter";
        case VK_ESCAPE: return L"Esc";
        case VK_BACK: return L"Backspace";
        case VK_INSERT: return L"Insert";
        case VK_DELETE: return L"Delete";
        case VK_HOME: return L"Home";
        case VK_END: return L"End";
        case VK_PRIOR: return L"PgUp";
        case VK_NEXT: return L"PgDn";
        case VK_LEFT: return L"←";
        case VK_RIGHT: return L"→";
        case VK_UP: return L"↑";
        case VK_DOWN: return L"↓";
        case VK_OEM_1: return L";";
        case VK_OEM_2: return L"/";
        case VK_OEM_3: return L"`";
        case VK_OEM_4: return L"[";
        case VK_OEM_5: return L"\\";
        case VK_OEM_6: return L"]";
        case VK_OEM_7: return L"'";
        case VK_OEM_PLUS: return L"=";
        case VK_OEM_MINUS: return L"-";
        case VK_OEM_COMMA: return L",";
        case VK_OEM_PERIOD: return L".";
        default: break;
    }
    UINT character = MapVirtualKeyW(vk, MAPVK_VK_TO_CHAR);
    if (character >= 32 && character < 0x10000) {
        wchar_t buffer[4] = {(wchar_t)(character & 0xFFFF), 0, 0, 0};
        CharUpperBuffW(buffer, 1);
        return buffer;
    }
    wchar_t fallback[16];
    wsprintfW(fallback, L"0x%02X", vk);
    return fallback;
}

void updateTrayIcon() {
    if (!g_trayVisible) return;
    std::wstring tooltip = std::wstring(kAppName) + L"  ·  " +
                           formatHotkey(g_settings.hotkeyMods, g_settings.hotkeyVk);
    if (tooltip.size() > 120) tooltip.resize(120);

    NOTIFYICONDATAW data = {};
    data.cbSize = sizeof(data);
    data.hWnd = g_controller;
    data.uID = kTrayId;
    data.uFlags = NIF_TIP | NIF_SHOWTIP;
    wcsncpy_s(data.szTip, tooltip.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &data);
}

void addTrayIcon() {
    int size = GetSystemMetrics(SM_CXSMICON);
    if (g_trayIcon) DestroyIcon(g_trayIcon);
    g_trayIcon = (HICON)LoadImageW(g_hinst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, size, size,
                                   LR_DEFAULTCOLOR);

    NOTIFYICONDATAW data = {};
    data.cbSize = sizeof(data);
    data.hWnd = g_controller;
    data.uID = kTrayId;
    data.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    data.uCallbackMessage = WM_APP_TRAY;
    data.hIcon = g_trayIcon;
    std::wstring tooltip = std::wstring(kAppName) + L"  ·  " +
                           formatHotkey(g_settings.hotkeyMods, g_settings.hotkeyVk);
    wcsncpy_s(data.szTip, tooltip.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_ADD, &data);
    data.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &data);
    g_trayVisible = true;
}

void removeTrayIcon() {
    if (!g_trayVisible) return;
    NOTIFYICONDATAW data = {};
    data.cbSize = sizeof(data);
    data.hWnd = g_controller;
    data.uID = kTrayId;
    Shell_NotifyIconW(NIM_DELETE, &data);
    g_trayVisible = false;
}

void showAboutWindow() {
    std::wstring message;
    message += L"PtFastQRcode  ";
    message += kAppVersion;
    message += L"\n\n";
    message += L"快捷键:" + formatHotkey(g_settings.hotkeyMods, g_settings.hotkeyVk) +
               L" 唤起二维码窗口\n";
    message += L"纯 Win32 原生实现,静态链接,单文件运行,无第三方运行库。\n";
    message += L"二维码编码器为本项目自带实现,已与参考实现逐模块比对并通过扫码回读验证。";
    MessageBoxW(g_controller, message.c_str(), L"关于 PtFastQRcode", MB_ICONINFORMATION | MB_OK);
}

void showTrayMenuImpl() {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    std::wstring hotkey = formatHotkey(g_settings.hotkeyMods, g_settings.hotkeyVk);
    std::wstring showLabel = L"显示二维码窗口\t" + hotkey;
    AppendMenuW(menu, MF_STRING, IDM_SHOW, showLabel.c_str());
    AppendMenuW(menu, MF_STRING, IDM_SETTINGS, L"设置…");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (g_settings.closeOnBlur ? MF_CHECKED : 0), IDM_CLICKOUTSIDE,
                L"点击窗口外自动关闭");
    AppendMenuW(menu, MF_STRING | (g_settings.autostart ? MF_CHECKED : 0), IDM_AUTOSTART,
                L"开机自启动");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_ABOUT, L"关于");
    AppendMenuW(menu, MF_STRING, IDM_EXIT, L"退出");
    SetMenuDefaultItem(menu, IDM_SHOW, FALSE);

    POINT cursor;
    GetCursorPos(&cursor);
    SetForegroundWindow(g_controller);
    int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, cursor.x,
                                 cursor.y, 0, g_controller, nullptr);
    PostMessageW(g_controller, WM_NULL, 0, 0);
    DestroyMenu(menu);

    switch (command) {
        case IDM_SHOW:
            showPopup();
            break;
        case IDM_SETTINGS:
            showSettingsWindow();
            break;
        case IDM_CLICKOUTSIDE:
            g_settings.closeOnBlur = !g_settings.closeOnBlur;
            saveSettings();
            if (g_popup) InvalidateRect(g_popup, nullptr, FALSE);
            break;
        case IDM_AUTOSTART:
            applyAutostart(!g_settings.autostart);
            break;
        case IDM_ABOUT:
            showAboutWindow();
            break;
        case IDM_EXIT:
            quitApp();
            break;
        default:
            break;
    }
}

LRESULT CALLBACK ControllerProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == g_taskbarCreatedMessage && g_taskbarCreatedMessage != 0) {
        g_trayVisible = false;
        addTrayIcon();
        return 0;
    }

    switch (message) {
        case WM_HOTKEY:
            if (wParam == g_hotkeyId) showPopup();
            return 0;

        case WM_APP_SHOW:
            showPopup();
            return 0;

        case WM_APP_TRAY: {
            // NOTIFYICON_VERSION_4 packs the icon id in the high word: LOWORD is the event.
            UINT event = LOWORD(lParam);
            switch (event) {
                case WM_LBUTTONUP:
                case WM_LBUTTONDBLCLK:
                case NIN_SELECT:
                case NIN_KEYSELECT:
                    showPopup();
                    return 0;
                case WM_RBUTTONUP:
                case WM_CONTEXTMENU:
                    showTrayMenuImpl();
                    return 0;
                default:
                    return 0;
            }
        }

        case WM_SETTINGCHANGE:
            if (g_settings.themeMode == 0 && lParam &&
                wcscmp((const wchar_t*)lParam, L"ImmersiveColorSet") == 0) {
                HWND popup = g_popup;
                g_popup = nullptr;
                if (popup) DestroyWindow(popup);
                if (g_settingsWnd) DestroyWindow(g_settingsWnd);
                applyTheme(g_settings.uiStyle);
                rebuildTrayIcon();
            }
            return 0;

        case WM_TIMER:
            if (wParam == kFirstShowTimer) {
                KillTimer(hwnd, kFirstShowTimer);
                showPopup();
            }
            return 0;

        case WM_DESTROY:
            removeTrayIcon();
            if (g_trayIcon) {
                DestroyIcon(g_trayIcon);
                g_trayIcon = nullptr;
            }
            PostQuitMessage(0);
            return 0;

        default:
            break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void enableDpiAwareness() {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        typedef BOOL(WINAPI * SetProcessDpiAwarenessContextFn)(HANDLE);
        auto setContext = (SetProcessDpiAwarenessContextFn)GetProcAddress(
            user32, "SetProcessDpiAwarenessContext");
        if (setContext) {
            if (setContext((HANDLE)-4)) return;  // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
        }
        typedef BOOL(WINAPI * SetProcessDPIAwareFn)(void);
        auto setAware = (SetProcessDPIAwareFn)GetProcAddress(user32, "SetProcessDPIAware");
        if (setAware && setAware()) return;
    }
}

int queryDpi() {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        typedef UINT(WINAPI * GetDpiForSystemFn)(void);
        auto getDpi = (GetDpiForSystemFn)GetProcAddress(user32, "GetDpiForSystem");
        if (getDpi) {
            UINT dpi = getDpi();
            if (dpi >= 96) return (int)dpi;
        }
    }
    HDC dc = GetDC(nullptr);
    int dpi = GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(nullptr, dc);
    return dpi > 0 ? dpi : 96;
}

// The UI is drawn at a single scale factor: the display DPI, capped so the tallest
// window (settings, 632 design px) still fits inside the work area with a margin.
void computeScale() {
    float dpiScale = queryDpi() / 96.0f;
    RECT work = {};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    float workWidth = (float)(work.right - work.left);
    float workHeight = (float)(work.bottom - work.top);
    if (workWidth < 200.0f || workHeight < 200.0f) {
        workWidth = (float)GetSystemMetrics(SM_CXSCREEN);
        workHeight = (float)GetSystemMetrics(SM_CYSCREEN);
    }
    float fitWidth = 0.88f * workWidth / 560.0f;
    float fitHeight = 0.88f * workHeight / 688.0f;
    g_scale = std::min(dpiScale, std::min(fitWidth, fitHeight));
    if (g_scale < 0.8f) g_scale = 0.8f;
    if (g_scale > 2.0f) g_scale = 2.0f;
}

void buildAccelerators() {
    ACCEL popupKeys[] = {
        {FVIRTKEY | FCONTROL, 'S', ID_POPUP_SAVE},
        {FVIRTKEY | FCONTROL, 'D', ID_POPUP_COPY},
        {FVIRTKEY, VK_ESCAPE, ID_POPUP_CLOSE},
    };
    g_popupAccel = CreateAcceleratorTableW(popupKeys, ARRAYSIZE(popupKeys));

    ACCEL settingsKeys[] = {
        {FVIRTKEY, VK_ESCAPE, ID_SETTINGS_CANCEL},
        {FVIRTKEY | FCONTROL, 'S', ID_SETTINGS_SAVE},
    };
    g_settingsAccel = CreateAcceleratorTableW(settingsKeys, ARRAYSIZE(settingsKeys));
}

}  // namespace

// ------------------------------------------------------------------ hotkey API

std::wstring formatHotkey(UINT mods, UINT vk) {
    std::wstring text;
    if (mods & MOD_WIN) text += L"Win + ";
    if (mods & MOD_CONTROL) text += L"Ctrl + ";
    if (mods & MOD_ALT) text += L"Alt + ";
    if (mods & MOD_SHIFT) text += L"Shift + ";
    text += keyName(vk);
    return text;
}

UINT normalizeHotkeyMods(UINT mods) {
    return mods & (MOD_ALT | MOD_CONTROL | MOD_SHIFT | MOD_WIN);
}

bool registerAppHotkey(UINT mods, UINT vk) {
    if (!g_controller) return false;
    UnregisterHotKey(g_controller, kHotkeyIdValue);
    g_hotkeyId = kHotkeyIdValue;
    if (RegisterHotKey(g_controller, kHotkeyIdValue, normalizeHotkeyMods(mods) | MOD_NOREPEAT, vk))
        return true;
    // Restore the previous registration so the app keeps working.
    RegisterHotKey(g_controller, kHotkeyIdValue,
                   normalizeHotkeyMods(g_settings.hotkeyMods) | MOD_NOREPEAT, g_settings.hotkeyVk);
    return false;
}

void unregisterAppHotkey() {
    if (g_controller) UnregisterHotKey(g_controller, kHotkeyIdValue);
}

// ------------------------------------------------------------------ app lifecycle

void rebuildTrayIcon() {
    if (!g_trayVisible) return;
    g_trayVisible = false;
    addTrayIcon();
}

void showTrayMenu() { showTrayMenuImpl(); }

void showAbout() { showAboutWindow(); }

void restartApplication() {
    std::wstring commandLine = L"\"" + currentExePath() + L"\" --show --restart";
    STARTUPINFOW startup = {sizeof(startup)};
    PROCESS_INFORMATION process = {};
    if (!CreateProcessW(nullptr, &commandLine[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                        &startup, &process)) {
        std::wstring message = L"重启失败,请手动退出后重新打开。错误码:" +
                               std::to_wstring(GetLastError());
        MessageBoxW(nullptr, message.c_str(), kAppName, MB_ICONERROR | MB_OK);
        return;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    quitApp();
}

void quitApp() {
    if (g_popup) {
        DestroyWindow(g_popup);
        g_popup = nullptr;
    }
    if (g_settingsWnd) DestroyWindow(g_settingsWnd);
    if (g_controller) DestroyWindow(g_controller);
    PostQuitMessage(0);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int showCommand) {
    g_hinst = instance;
    (void)showCommand;

    enableDpiAwareness();
    computeScale();

    OleInitialize(nullptr);

    Gdiplus::GdiplusStartupInput gdiplusInput;
    ULONG_PTR gdiplusToken = 0;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusInput, nullptr);

    loadSettings();
    applyTheme(g_settings.uiStyle);

    std::wstring arguments = commandLine ? commandLine : L"";
    bool startHidden = arguments.find(L"--startup") != std::wstring::npos;
    bool requestShow = arguments.find(L"--show") != std::wstring::npos;
    bool restartMode = arguments.find(L"--restart") != std::wstring::npos;

    // A restarted instance waits briefly for its predecessor to release the mutex.
    HANDLE mutex = nullptr;
    bool alreadyRunning = false;
    const int attempts = restartMode ? 60 : 1;
    for (int attempt = 0; attempt < attempts; ++attempt) {
        if (mutex) CloseHandle(mutex);
        mutex = CreateMutexW(nullptr, FALSE, L"Local\\PtFastQRcode.SingleInstance");
        alreadyRunning = !mutex || GetLastError() == ERROR_ALREADY_EXISTS;
        if (!alreadyRunning) break;
        if (attempt + 1 < attempts) Sleep(100);
    }

    if (alreadyRunning) {
        HWND existing = FindWindowW(kWndClassController, nullptr);
        if (existing) {
            PostMessageW(existing, WM_APP_SHOW, 0, 0);
        } else {
            MessageBoxW(nullptr, L"PtFastQRcode 已在运行,但找不到它的窗口。", kAppName,
                        MB_ICONWARNING | MB_OK);
        }
        if (mutex) CloseHandle(mutex);
        Gdiplus::GdiplusShutdown(gdiplusToken);
        OleUninitialize();
        return 0;
    }

    g_taskbarCreatedMessage = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = ControllerProc;
    wc.hInstance = g_hinst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kWndClassController;
    wc.hIcon = (HICON)LoadImageW(g_hinst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, 32, 32, 0);
    RegisterClassExW(&wc);

    g_controller = CreateWindowExW(WS_EX_TOOLWINDOW, kWndClassController, kAppName, WS_POPUP, 0, 0,
                                   0, 0, nullptr, nullptr, g_hinst, nullptr);
    if (!g_controller) return 1;

    addTrayIcon();
    buildAccelerators();

    if (!registerAppHotkey(g_settings.hotkeyMods, g_settings.hotkeyVk)) {
        std::wstring message = L"快捷键 " + formatHotkey(g_settings.hotkeyMods, g_settings.hotkeyVk) +
                               L" 注册失败,可能被其他程序占用。\n可以在托盘菜单的“设置”里换一个组合键。";
        MessageBoxW(nullptr, message.c_str(), kAppName, MB_ICONWARNING | MB_OK);
    }

    if (requestShow) {
        showPopup();
    } else if (!startHidden) {
        SetTimer(g_controller, kFirstShowTimer, 260, nullptr);
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (g_popup && IsWindowVisible(g_popup) && g_popupAccel &&
            TranslateAcceleratorW(g_popup, g_popupAccel, &msg))
            continue;
        if (g_settingsWnd && IsWindowVisible(g_settingsWnd) && g_settingsAccel &&
            TranslateAcceleratorW(g_settingsWnd, g_settingsAccel, &msg))
            continue;
        if (g_popup && IsWindowVisible(g_popup) && IsDialogMessageW(g_popup, &msg)) continue;
        if (g_settingsWnd && IsWindowVisible(g_settingsWnd) && IsDialogMessageW(g_settingsWnd, &msg))
            continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_popupAccel) DestroyAcceleratorTable(g_popupAccel);
    if (g_settingsAccel) DestroyAcceleratorTable(g_settingsAccel);
    destroyFonts();
    Gdiplus::GdiplusShutdown(gdiplusToken);
    OleUninitialize();
    if (mutex) CloseHandle(mutex);
    return (int)msg.wParam;
}
