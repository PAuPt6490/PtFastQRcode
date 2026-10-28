#include "app.h"

#include "text_util.h"

#include <commctrl.h>
#include <shlobj.h>
#include <uxtheme.h>
#include <vssym32.h>

#include <algorithm>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")

// ------------------------------------------------------------------ globals

HINSTANCE g_hinst = nullptr;
HWND g_controller = nullptr;
HWND g_popup = nullptr;
HWND g_settingsWnd = nullptr;
bool g_settingsOpen = false;
float g_scale = 1.0f;
UINT g_hotkeyId = 1;
static HFONT g_glyphFont = nullptr;   // Segoe icon font, created lazily
static bool g_paintBuffered = false;  // only during interactive size/move

const wchar_t* kAppName = L"PtFastQRcode";
const wchar_t* kAppVersion = L"1.0.0";
const wchar_t* kWndClassController = L"PtFastQRcodeController";
const wchar_t* kWndClassPopup = L"PtFastQRcodePopup";
const wchar_t* kWndClassSettings = L"PtFastQRcodeSettings";
const wchar_t* kWndClassHotkey = L"PtFastQRcodeHotkey";
const wchar_t* kRegRoot = L"Software\\PtFastQRcode";
const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t* kRunValue = L"PtFastQRcode";

Settings g_settings;
Theme g_theme;
Fonts g_fonts;

// ------------------------------------------------------------------ helpers

std::wstring currentExePath() {
    wchar_t buffer[MAX_PATH * 2] = {0};
    GetModuleFileNameW(nullptr, buffer, ARRAYSIZE(buffer));
    return buffer;
}

std::wstring defaultSaveDir() {
    PWSTR path = nullptr;
    std::wstring result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Pictures, 0, nullptr, &path))) {
        result = path;
        result += L"\\PtFastQRcode";
        CoTaskMemFree(path);
    } else {
        result = L".";
    }
    return result;
}

void ensureDirectory(const std::wstring& path) {
    if (path.empty()) return;
    DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY)) return;
    CreateDirectoryW(path.c_str(), nullptr);
}

// ------------------------------------------------------------------ settings

static bool regReadBool(HKEY key, const wchar_t* name, bool fallback) {
    DWORD value = 0, size = sizeof(value), type = 0;
    if (RegQueryValueExW(key, name, nullptr, &type, (BYTE*)&value, &size) != ERROR_SUCCESS ||
        type != REG_DWORD)
        return fallback;
    return value != 0;
}

static int regReadInt(HKEY key, const wchar_t* name, int fallback) {
    DWORD value = 0, size = sizeof(value), type = 0;
    if (RegQueryValueExW(key, name, nullptr, &type, (BYTE*)&value, &size) != ERROR_SUCCESS ||
        type != REG_DWORD)
        return fallback;
    return (int)value;
}

static std::wstring regReadString(HKEY key, const wchar_t* name, const std::wstring& fallback) {
    wchar_t buffer[1024] = {0};
    DWORD size = sizeof(buffer) - sizeof(wchar_t), type = 0;
    if (RegQueryValueExW(key, name, nullptr, &type, (BYTE*)buffer, &size) != ERROR_SUCCESS ||
        type != REG_SZ)
        return fallback;
    buffer[ARRAYSIZE(buffer) - 1] = 0;
    return buffer;
}



void loadSettings() {
    g_settings.hotkeyMods = MOD_CONTROL | MOD_ALT;
    g_settings.hotkeyVk = 'Q';
    g_settings.closeOnBlur = true;
    g_settings.autoPaste = true;
    g_settings.closeAfterSave = true;
    g_settings.eccLevel = 1;
    g_settings.qrSize = 360;
    g_settings.uiStyle = 1;
    g_settings.themeMode = 0;
    g_settings.popupWidth = 400;
    g_settings.popupHeight = 470;
    g_settings.saveDir = defaultSaveDir();

    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegRoot, 0, KEY_READ, &key) == ERROR_SUCCESS) {
        g_settings.hotkeyMods = (UINT)regReadInt(key, L"HotkeyMods", (int)g_settings.hotkeyMods);
        g_settings.hotkeyVk = (UINT)regReadInt(key, L"HotkeyVk", (int)g_settings.hotkeyVk);
        g_settings.closeOnBlur = regReadBool(key, L"CloseOnBlur", g_settings.closeOnBlur);
        g_settings.autoPaste = regReadBool(key, L"AutoPaste", g_settings.autoPaste);
        g_settings.closeAfterSave = regReadBool(key, L"CloseAfterSave", g_settings.closeAfterSave);
        g_settings.eccLevel = regReadInt(key, L"EccLevel", g_settings.eccLevel);
        g_settings.qrSize = regReadInt(key, L"QrSize", g_settings.qrSize);
        g_settings.uiStyle = regReadInt(key, L"UiStyle", g_settings.uiStyle);
        g_settings.themeMode = regReadInt(key, L"ThemeMode", g_settings.themeMode);
        g_settings.popupWidth = regReadInt(key, L"PopupWidth", g_settings.popupWidth);
        g_settings.popupHeight = regReadInt(key, L"PopupHeight", g_settings.popupHeight);
        g_settings.saveDir = regReadString(key, L"SaveDir", g_settings.saveDir);
        RegCloseKey(key);
    }

    if (g_settings.hotkeyVk == 0) g_settings.hotkeyVk = 'Q';
    g_settings.hotkeyMods = normalizeHotkeyMods(g_settings.hotkeyMods);
    g_settings.eccLevel = std::max(0, std::min(3, g_settings.eccLevel));
    if (g_settings.uiStyle > 1) g_settings.uiStyle = 1;  // UWP style was removed
    g_settings.themeMode = std::max(0, std::min(2, g_settings.themeMode));
    g_settings.qrSize = std::max(160, std::min(1200, g_settings.qrSize));
    g_settings.popupWidth = std::max(340, std::min(1400, g_settings.popupWidth));
    g_settings.popupHeight = std::max(400, std::min(1600, g_settings.popupHeight));
    if (g_settings.saveDir.empty()) g_settings.saveDir = defaultSaveDir();
    g_settings.autostart = isAutostartEnabled();
}

void saveSettings() {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegRoot, 0, nullptr, 0, KEY_WRITE, nullptr, &key,
                        nullptr) != ERROR_SUCCESS)
        return;

    auto writeDword = [&](const wchar_t* name, DWORD value) {
        RegSetValueExW(key, name, 0, REG_DWORD, (const BYTE*)&value, sizeof(value));
    };

    writeDword(L"HotkeyMods", g_settings.hotkeyMods);
    writeDword(L"HotkeyVk", g_settings.hotkeyVk);
    writeDword(L"CloseOnBlur", g_settings.closeOnBlur ? 1 : 0);
    writeDword(L"AutoPaste", g_settings.autoPaste ? 1 : 0);
    writeDword(L"CloseAfterSave", g_settings.closeAfterSave ? 1 : 0);
    writeDword(L"EccLevel", (DWORD)g_settings.eccLevel);
    writeDword(L"QrSize", (DWORD)g_settings.qrSize);
    writeDword(L"UiStyle", (DWORD)g_settings.uiStyle);
    writeDword(L"ThemeMode", (DWORD)g_settings.themeMode);
    writeDword(L"PopupWidth", (DWORD)g_settings.popupWidth);
    writeDword(L"PopupHeight", (DWORD)g_settings.popupHeight);
    RegSetValueExW(key, L"SaveDir", 0, REG_SZ, (const BYTE*)g_settings.saveDir.c_str(),
                   (DWORD)((g_settings.saveDir.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
}

bool isAutostartEnabled() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &key) != ERROR_SUCCESS)
        return false;
    wchar_t buffer[MAX_PATH * 2] = {0};
    DWORD size = sizeof(buffer) - sizeof(wchar_t), type = 0;
    bool found = RegQueryValueExW(key, kRunValue, nullptr, &type, (BYTE*)buffer, &size) ==
                     ERROR_SUCCESS &&
                 type == REG_SZ;
    RegCloseKey(key);
    return found && buffer[0] != 0;
}

void applyAutostart(bool enable) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_WRITE, nullptr, &key,
                        nullptr) != ERROR_SUCCESS)
        return;
    if (enable) {
        std::wstring command = L"\"" + currentExePath() + L"\" --startup";
        RegSetValueExW(key, kRunValue, 0, REG_SZ, (const BYTE*)command.c_str(),
                       (DWORD)((command.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(key, kRunValue);
    }
    RegCloseKey(key);
    g_settings.autostart = enable;
}

// ------------------------------------------------------------------ theme

static const Theme kThemes[2] = {
    {L"Win32",
     RGB(240, 240, 240), RGB(240, 240, 240), RGB(255, 255, 255), RGB(160, 160, 160),
     RGB(16, 16, 16), RGB(96, 96, 96), RGB(10, 100, 180), RGB(255, 255, 255),
     RGB(8, 84, 152), RGB(225, 225, 225), RGB(229, 241, 251), RGB(173, 173, 173),
     RGB(255, 255, 255), RGB(122, 122, 122), RGB(213, 213, 213), RGB(200, 60, 60),
     2, 16, false, true},
    {L"WinUI 3",
     RGB(32, 32, 32), RGB(38, 38, 38), RGB(43, 43, 43), RGB(58, 58, 58),
     RGB(255, 255, 255), RGB(160, 160, 160), RGB(76, 194, 255), RGB(6, 26, 38),
     RGB(96, 205, 255), RGB(45, 45, 45), RGB(56, 56, 56), RGB(58, 58, 58),
     RGB(39, 39, 39), RGB(62, 62, 62), RGB(60, 60, 60), RGB(196, 43, 28),
     8, 16, true, false},
};

bool systemPrefersDark() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0,
                      KEY_READ, &key) != ERROR_SUCCESS)
        return false;
    DWORD value = 1, size = sizeof(value), type = 0;
    bool dark = false;
    if (RegQueryValueExW(key, L"AppsUseLightTheme", nullptr, &type, (BYTE*)&value, &size) ==
            ERROR_SUCCESS &&
        type == REG_DWORD)
        dark = value == 0;
    RegCloseKey(key);
    return dark;
}

// The user's accent colour, taken from the window manager (official API) with the
// classic system highlight colour as fallback.
COLORREF systemAccentColor() {
    // The system highlight colour is the user's accent colour, exposed by plain GDI.
    return GetSysColor(COLOR_HIGHLIGHT);
}

COLORREF contrastingTextColor(COLORREF background) {
    int luminance = (GetRValue(background) * 299 + GetGValue(background) * 587 +
                     GetBValue(background) * 114) /
                    1000;
    return luminance >= 150 ? RGB(16, 16, 16) : RGB(255, 255, 255);
}

static COLORREF lighten(COLORREF color, int amount) {
    int r = std::min(255, GetRValue(color) + amount);
    int g = std::min(255, GetGValue(color) + amount);
    int b = std::min(255, GetBValue(color) + amount);
    return RGB(r, g, b);
}

// Resolves "follow system" into a concrete light/dark decision.
bool themeIsDark() {
    switch (g_settings.themeMode) {
        case 1: return false;
        case 2: return true;
        default: return systemPrefersDark();
    }
}

static void applyFluentPalette(bool dark) {
    if (dark) {
        g_theme.bg = RGB(32, 32, 32);
        g_theme.card = RGB(43, 43, 43);
        g_theme.cardBorder = RGB(60, 60, 60);
        g_theme.text = RGB(255, 255, 255);
        g_theme.textDim = RGB(160, 160, 160);
        g_theme.fieldBg = RGB(42, 42, 42);
        g_theme.fieldBorder = RGB(70, 70, 70);
        g_theme.buttonBg = RGB(46, 46, 46);
        g_theme.buttonHover = RGB(58, 58, 58);
        g_theme.buttonBorder = RGB(64, 64, 64);
        g_theme.hover = RGB(58, 58, 58);
        g_theme.danger = RGB(196, 43, 28);
    } else {
        g_theme.bg = RGB(243, 243, 243);
        g_theme.card = RGB(255, 255, 255);
        g_theme.cardBorder = RGB(225, 225, 225);
        g_theme.text = RGB(26, 26, 26);
        g_theme.textDim = RGB(96, 96, 96);
        g_theme.fieldBg = RGB(255, 255, 255);
        g_theme.fieldBorder = RGB(200, 200, 200);
        g_theme.buttonBg = RGB(251, 251, 251);
        g_theme.buttonHover = RGB(240, 240, 240);
        g_theme.buttonBorder = RGB(214, 214, 214);
        g_theme.hover = RGB(233, 233, 233);
        g_theme.danger = RGB(196, 43, 28);
    }
    g_theme.bgHeader = g_theme.bg;
    g_theme.dark = dark;
}

void applyTheme(int style) {
    g_settings.uiStyle = std::max(0, std::min(1, style));
    g_theme = kThemes[g_settings.uiStyle];
    const bool dark = themeIsDark();
    g_theme.dark = dark;

    if (g_theme.flatClick) {
        if (dark) {
            // Win32 structure in the dark surface colours Windows uses for its own dialogs.
            g_theme.bg = RGB(32, 32, 32);
            g_theme.card = RGB(45, 45, 45);
            g_theme.cardBorder = RGB(64, 64, 64);
            g_theme.text = RGB(240, 240, 240);
            g_theme.textDim = RGB(160, 160, 160);
            g_theme.fieldBg = RGB(45, 45, 45);
            g_theme.fieldBorder = RGB(74, 74, 74);
            g_theme.buttonBg = RGB(54, 54, 54);
            g_theme.buttonHover = RGB(66, 66, 66);
            g_theme.buttonBorder = RGB(78, 78, 78);
            g_theme.hover = RGB(64, 64, 64);
            g_theme.bgHeader = g_theme.bg;
        } else {
            // Official system colours and metrics; controls are drawn with UxTheme.
            g_theme.bg = GetSysColor(COLOR_3DFACE);
            g_theme.bgHeader = g_theme.bg;
            g_theme.card = GetSysColor(COLOR_WINDOW);
            g_theme.cardBorder = GetSysColor(COLOR_3DSHADOW);
            g_theme.text = GetSysColor(COLOR_BTNTEXT);
            g_theme.textDim = GetSysColor(COLOR_GRAYTEXT);
            g_theme.fieldBg = GetSysColor(COLOR_WINDOW);
            g_theme.fieldBorder = GetSysColor(COLOR_3DSHADOW);
            g_theme.buttonBg = GetSysColor(COLOR_3DFACE);
            g_theme.buttonHover = GetSysColor(COLOR_3DHIGHLIGHT);
            g_theme.buttonBorder = GetSysColor(COLOR_3DSHADOW);
            g_theme.hover = GetSysColor(COLOR_3DHIGHLIGHT);
        }
        g_theme.accent = dark ? systemAccentColor() : GetSysColor(COLOR_HIGHLIGHT);
    } else {
        applyFluentPalette(dark);
        g_theme.accent = systemAccentColor();
    }
    g_theme.accentText = contrastingTextColor(g_theme.accent);
    g_theme.accentHover = lighten(g_theme.accent, dark ? 26 : 18);

    destroyFonts();   // recreated lazily by the next window
}

// Rounds the window by clipping its own region, so the shape does not depend on the
// compositor's window-corner support (identical on Windows 10 and 11). Classic theme and
// anything below 3 design px stay square.
void applyWindowShape(HWND hwnd) {
    if (!hwnd) return;
    RECT client;
    GetClientRect(hwnd, &client);
    int radius = g_theme.flatClick ? 0 : S(g_theme.radius);
    if (radius < 3 || client.right <= 0 || client.bottom <= 0) {
        SetWindowRgn(hwnd, nullptr, TRUE);
        return;
    }
    HRGN region = CreateRoundRectRgn(0, 0, client.right + 1, client.bottom + 1, radius * 2,
                                     radius * 2);
    SetWindowRgn(hwnd, region, TRUE);
}

// ------------------------------------------------------------------ system icon font

static HFONT ensureGlyphFont() {
    if (g_glyphFont) return g_glyphFont;
    static const wchar_t* candidates[] = {L"Segoe Fluent Icons", L"Segoe MDL2 Assets"};
    HDC hdc = GetDC(nullptr);
    for (const wchar_t* name : candidates) {
        HFONT font = CreateFontW(-S(16), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                 OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                 DEFAULT_PITCH | FF_DONTCARE, name);
        if (!font) continue;
        HGDIOBJ old = SelectObject(hdc, font);
        wchar_t face[LF_FACESIZE] = {0};
        GetTextFaceW(hdc, LF_FACESIZE, face);
        SelectObject(hdc, old);
        if (tu::compareNoCase(face, name) == 0) {
            g_glyphFont = font;
            break;
        }
        DeleteObject(font);
    }
    ReleaseDC(nullptr, hdc);
    return g_glyphFont;
}

bool hasSystemIconFont() { return ensureGlyphFont() != nullptr; }

void drawSystemGlyph(HDC hdc, const RECT& rc, wchar_t glyph, COLORREF color) {
    HFONT font = ensureGlyphFont();
    if (!font) return;
    wchar_t text[2] = {glyph, 0};
    RECT target = rc;
    drawTextGdi(hdc, font, color, text, target,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

void paintFieldFrame(HWND hwnd, HDC hdc, const RECT& rc, int radius) {
    if (g_theme.flatClick) {
        HTHEME theme = OpenThemeData(hwnd, L"EDIT");
        if (theme) {
            RECT frame = rc;
            DrawThemeBackground(theme, hdc, EP_EDITTEXT, ETS_NORMAL, &frame, nullptr);
            CloseThemeData(theme);
            return;
        }
    }
    drawRoundRect(hdc, rc, radius, g_theme.fieldBg, g_theme.fieldBorder, 1);
}

// ------------------------------------------------------------------ COM

namespace {
bool g_comReady = false;
}

bool ensureCom() {
    if (g_comReady) return true;
    g_comReady = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE));
    return g_comReady;
}

void releaseCom() {
    if (!g_comReady) return;
    CoUninitialize();
    g_comReady = false;
}

// ------------------------------------------------------------------ fonts

static const wchar_t* pickFontFamily() {
    static const wchar_t* candidates[] = {L"Microsoft YaHei UI", L"Microsoft YaHei", L"Segoe UI"};
    HDC hdc = GetDC(nullptr);
    for (const wchar_t* name : candidates) {
        HFONT font = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                 OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                 DEFAULT_PITCH | FF_DONTCARE, name);
        if (font) {
            HGDIOBJ old = SelectObject(hdc, font);
            wchar_t face[LF_FACESIZE] = {0};
            GetTextFaceW(hdc, LF_FACESIZE, face);
            SelectObject(hdc, old);
            DeleteObject(font);
            if (tu::compareNoCase(face, name) == 0) {
                ReleaseDC(nullptr, hdc);
                return name;
            }
        }
    }
    ReleaseDC(nullptr, hdc);
    return L"Segoe UI";
}

static HFONT makeFont(int pixelHeight, int weight) {
    int delta = g_theme.flatClick ? -1 : 0;   // the classic look uses slightly tighter text
    return CreateFontW(-(pixelHeight + delta), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH | FF_DONTCARE, pickFontFamily());
}

void ensureFonts() {
    if (g_fonts.title) return;
    g_fonts.title = makeFont(S(16), FW_SEMIBOLD);
    g_fonts.body = makeFont(S(14), FW_NORMAL);
    g_fonts.hint = makeFont(S(12), FW_NORMAL);
    g_fonts.bold = makeFont(S(13), FW_SEMIBOLD);
}

void destroyFonts() {
    HFONT fonts[] = {g_fonts.title, g_fonts.body, g_fonts.hint, g_fonts.bold};
    for (HFONT font : fonts) {
        if (font) DeleteObject(font);
    }
    g_fonts.title = g_fonts.body = g_fonts.hint = g_fonts.bold = nullptr;
    if (g_glyphFont) {
        DeleteObject(g_glyphFont);
        g_glyphFont = nullptr;
    }
}

// ------------------------------------------------------------------ drawing

// Rounded rectangle through GDI. Pass CLR_NONE as fill for a hollow frame,
// CLR_NONE as stroke for a borderless fill.
void drawRoundRect(HDC hdc, const RECT& rc, int radius, COLORREF fill, COLORREF stroke,
                   int strokeWidth) {
    if (rc.right - rc.left <= 0 || rc.bottom - rc.top <= 0) return;

    const bool filled = fill != CLR_NONE;
    const bool stroked = stroke != CLR_NONE && strokeWidth > 0;

    HBRUSH brush = filled ? CreateSolidBrush(fill) : (HBRUSH)GetStockObject(NULL_BRUSH);
    HPEN pen = stroked ? CreatePen(PS_SOLID, strokeWidth, stroke) : (HPEN)GetStockObject(NULL_PEN);
    HGDIOBJ oldBrush = SelectObject(hdc, brush);
    HGDIOBJ oldPen = SelectObject(hdc, pen);

    // RoundRect's last two parameters are the corner ellipse size; 0 gives square corners.
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, radius * 2, radius * 2);

    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    if (stroked) DeleteObject(pen);
    if (filled) DeleteObject(brush);
}

void drawTextGdi(HDC hdc, HFONT font, COLORREF color, const wchar_t* text, RECT rect,
                 UINT format) {
    HGDIOBJ oldFont = SelectObject(hdc, font);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, color);
    DrawTextW(hdc, text, -1, &rect, format);
    SelectObject(hdc, oldFont);
}

int textWidth(HDC hdc, HFONT font, const wchar_t* text) {
    HGDIOBJ oldFont = SelectObject(hdc, font);
    SIZE size = {0, 0};
    GetTextExtentPoint32W(hdc, text, (int)wcslen(text), &size);
    SelectObject(hdc, oldFont);
    return size.cx;
}

void setPaintBuffered(bool buffered) { g_paintBuffered = buffered; }

bool paintIsBuffered() { return g_paintBuffered; }

PaintBuffer beginPaintBuffer(HWND hwnd, const RECT& client) {
    PaintBuffer buffer = {};
    HDC windowDc = GetDC(hwnd);
    buffer.dc = CreateCompatibleDC(windowDc);
    buffer.bitmap = CreateCompatibleBitmap(windowDc, client.right, client.bottom);
    buffer.previousBitmap = SelectObject(buffer.dc, buffer.bitmap);
    ReleaseDC(hwnd, windowDc);

    HBRUSH brush = CreateSolidBrush(g_theme.bg);
    FillRect(buffer.dc, &client, brush);
    DeleteObject(brush);
    return buffer;
}

void endPaintBuffer(HWND hwnd, PaintBuffer& buffer) {
    RECT client;
    GetClientRect(hwnd, &client);
    HDC windowDc = GetDC(hwnd);
    BitBlt(windowDc, 0, 0, client.right, client.bottom, buffer.dc, 0, 0, SRCCOPY);
    ReleaseDC(hwnd, windowDc);

    // Deselect first: a bitmap still selected into a DC cannot be destroyed.
    SelectObject(buffer.dc, buffer.previousBitmap);
    DeleteObject(buffer.bitmap);
    DeleteDC(buffer.dc);
    buffer.dc = nullptr;
    buffer.bitmap = nullptr;
}

void paintWindowBackdrop(HDC hdc, const RECT& client, bool rounded) {
    HBRUSH background = CreateSolidBrush(g_theme.bg);
    FillRect(hdc, &client, background);
    DeleteObject(background);

    if (!rounded) return;
    drawRoundRect(hdc, client, g_theme.radius, CLR_NONE, g_theme.cardBorder, 1);
}

// ------------------------------------------------------------------ buttons

static LRESULT CALLBACK ButtonSubclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                            UINT_PTR, DWORD_PTR) {
    switch (message) {
        case WM_MOUSEMOVE: {
            if (!GetPropW(hwnd, L"fastqr.hot")) {
                SetPropW(hwnd, L"fastqr.hot", (HANDLE)1);
                TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, hwnd, 0};
                TrackMouseEvent(&track);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            break;
        }
        case WM_MOUSELEAVE: {
            RemovePropW(hwnd, L"fastqr.hot");
            InvalidateRect(hwnd, nullptr, FALSE);
            break;
        }
        case WM_SETCURSOR: {
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        }
        default:
            break;
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

void installButtonSubclass(HWND button) {
    SetWindowSubclass(button, ButtonSubclassProc, 1, 0);
}

bool buttonIsHot(HWND button) {
    return GetPropW(button, L"fastqr.hot") != nullptr;
}
