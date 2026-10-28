#include "app.h"

#include <commdlg.h>   // types only: comdlg32 is resolved at run time

#include "text_util.h"

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
    tu::copy(data.szTip, ARRAYSIZE(data.szTip), tooltip.c_str());
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
    tu::copy(data.szTip, ARRAYSIZE(data.szTip), tooltip.c_str());
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
        wchar_t code[32];
        tu::formatUnsigned(code, ARRAYSIZE(code), (unsigned long)GetLastError());
        std::wstring message = L"重启失败,请手动退出后重新打开。错误码:" + std::wstring(code);
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

// comdlg32 is resolved on demand: importing it would load the dialog stack into the
// tray process at startup, which is exactly what running the dialog elsewhere avoids.
typedef BOOL(WINAPI* GetSaveFileNameWFn)(LPOPENFILENAMEW);

static GetSaveFileNameWFn resolveSaveDialog() {
    static GetSaveFileNameWFn resolve = nullptr;
    if (!resolve) {
        HMODULE module = LoadLibraryW(L"comdlg32.dll");
        if (module) resolve = (GetSaveFileNameWFn)GetProcAddress(module, "GetSaveFileNameW");
    }
    return resolve;
}

// Reads one quoted argument starting at cursor; advances the cursor past it.
static std::wstring takeQuotedArgument(const wchar_t*& cursor) {
    while (*cursor == L' ' || *cursor == L'\t') ++cursor;
    std::wstring value;
    if (*cursor == L'"') {
        ++cursor;
        while (*cursor && *cursor != L'"') value.push_back(*cursor++);
        if (*cursor == L'"') ++cursor;
    } else {
        while (*cursor && *cursor != L' ' && *cursor != L'\t') value.push_back(*cursor++);
    }
    return value;
}

// Headless check of the encode-and-save path: encodes a fixed string and writes it as
// PNG. Used to verify the WIC path on machines where the UI cannot be driven.
static int runSelfTest(const std::wstring& arguments) {
    const wchar_t* cursor = arguments.c_str();
    takeQuotedArgument(cursor);  // --selftest
    const std::wstring target = takeQuotedArgument(cursor);
    if (target.empty()) return 2;

    const std::wstring report = target + L".txt";
    std::wstring result;

    qr::Symbol symbol;
    std::wstring error;
    qr::EncodeOptions options;
    options.ecc = qr::Ecc::Medium;
    if (!qr::encodeText(L"https://example.com/selftest", options, symbol, error)) {
        result = L"encode-failed: " + error;
    } else {
        result = L"symbol=" + std::to_wstring(symbol.size);
        const bool saved = saveQrPng(symbol, 360, target);
        result += saved ? L" save=ok" : L" save=failed";
        HANDLE file = CreateFileW(target.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            LARGE_INTEGER size = {};
            GetFileSizeEx(file, &size);
            CloseHandle(file);
            result += L" bytes=" + std::to_wstring(size.QuadPart);
        } else {
            result += L" file=missing";
        }
    }

    HANDLE out = CreateFileW(report.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    if (out != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(out, result.c_str(), (DWORD)(result.size() * sizeof(wchar_t)), &written, nullptr);
        CloseHandle(out);
    }
    return 0;
}

// Save-dialog helper: runs in its own process so the shell extensions the common dialog
// loads are released the moment it exits. Writes the chosen path (UTF-16) to resultPath.
static int runSaveDialogHelper(const std::wstring& arguments) {
    const wchar_t* cursor = arguments.c_str();
    takeQuotedArgument(cursor);  // --pick-save
    const std::wstring resultPath = takeQuotedArgument(cursor);
    const std::wstring initialDir = takeQuotedArgument(cursor);
    const std::wstring suggestedName = takeQuotedArgument(cursor);
    if (resultPath.empty()) return 1;

    ensureCom();

    std::vector<wchar_t> buffer(MAX_PATH * 4, 0);
    const std::wstring initial =
        initialDir.empty() ? suggestedName : initialDir + L"\\" + suggestedName;
    tu::copy(buffer.data(), buffer.size(), initial.c_str());

    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFilter = L"PNG 图片 (*.png)\0*.png\0所有文件 (*.*)\0*.*\0";
    dialog.lpstrFile = buffer.data();
    dialog.nMaxFile = (DWORD)buffer.size();
    dialog.lpstrDefExt = L"png";
    dialog.lpstrInitialDir = initialDir.c_str();
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    const GetSaveFileNameWFn showDialog = resolveSaveDialog();
    const BOOL accepted = showDialog ? showDialog(&dialog) : FALSE;

    HANDLE file = CreateFileW(resultPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        if (accepted) {
            DWORD written = 0;
            WriteFile(file, buffer.data(), (DWORD)(wcslen(buffer.data()) * sizeof(wchar_t)),
                      &written, nullptr);
        }
        CloseHandle(file);
    }
    return 0;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int showCommand) {
    g_hinst = instance;
    (void)showCommand;

    enableDpiAwareness();
    computeScale();

    // COM stays uninitialised until something needs it (see ensureCom).

    loadSettings();
    applyTheme(g_settings.theme);

    std::wstring arguments = commandLine ? commandLine : L"";
    if (tu::contains(arguments.c_str(), L"--pick-save")) return runSaveDialogHelper(arguments);
    if (tu::contains(arguments.c_str(), L"--selftest")) return runSelfTest(arguments);
    bool startHidden = tu::contains(arguments.c_str(), L"--startup");
    bool requestShow = tu::contains(arguments.c_str(), L"--show");
    bool restartMode = tu::contains(arguments.c_str(), L"--restart");

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
        releaseCom();
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
    releaseCom();
    if (mutex) CloseHandle(mutex);
    return (int)msg.wParam;
}
