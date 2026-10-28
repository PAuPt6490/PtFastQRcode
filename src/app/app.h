// PtFastQRcode - shared application declarations.
#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <string>
#include <vector>

#include "qr/qrcode.h"

// ------------------------------------------------------------------ app info

extern const wchar_t* kAppName;
extern const wchar_t* kAppVersion;
extern const wchar_t* kWndClassController;
extern const wchar_t* kWndClassPopup;
extern const wchar_t* kWndClassSettings;
extern const wchar_t* kWndClassHotkey;

#define WM_APP_SHOW (WM_APP + 10)
#define WM_APP_TRAY (WM_APP + 11)
#define WM_APP_DESTROY (WM_APP + 12)
#define WM_APP_HOTKEY_CHANGED (WM_APP + 12)

// Control ids that the message loop needs for its accelerator tables.
#define ID_POPUP_SAVE 2002
#define ID_POPUP_COPY 2003
#define ID_POPUP_CLOSE 2006
#define ID_SETTINGS_CANCEL 3062
#define ID_SETTINGS_SAVE 3063

extern HINSTANCE g_hinst;
extern HWND g_controller;
extern HWND g_popup;
extern HWND g_settingsWnd;
extern bool g_settingsOpen;   // settings window exists and is visible
extern float g_scale;         // design px -> device px (follows DPI, capped to the work area)
extern UINT g_hotkeyId;

// Design pixel -> device pixel. 960-dpi style scaling, but never so large that the
// settings window cannot fit on screen.
inline int S(int logical) { return (int)(logical * g_scale + 0.5f); }

// ------------------------------------------------------------------ settings

struct Settings {
    UINT hotkeyMods;
    UINT hotkeyVk;
    bool closeOnBlur;
    bool autoPaste;
    bool closeAfterSave;
    bool autostart;
    int eccLevel;   // index into qr::Ecc: 0..3
    int qrSize;     // device independent pixels of the exported/saved image
    int theme;      // 0 = Win32 classic, 1 = follow system, 2 = WinUI 3 light, 3 = WinUI 3 dark
    int popupWidth;   // remembered popup size in design pixels
    int popupHeight;
    std::wstring saveDir;
};

extern Settings g_settings;

bool systemPrefersDark();
bool themeIsDark();
int  defaultTheme();
inline int themeStyle(int theme) { return theme == 0 ? 0 : 1; }
// 0 = classic, 1 = follow system, 2 = light, 3 = dark (resolved by themeIsDark()).
inline bool themeIsDarkTheme(int theme) { return theme == 3; }

void loadSettings();
void saveSettings();
void applyAutostart(bool enable);
bool isAutostartEnabled();
std::wstring currentExePath();
std::wstring defaultSaveDir();

// ------------------------------------------------------------------- theme

struct Theme {
    const wchar_t* name;
    COLORREF bg;
    COLORREF bgHeader;
    COLORREF card;
    COLORREF cardBorder;
    COLORREF text;
    COLORREF textDim;
    COLORREF accent;
    COLORREF accentText;
    COLORREF accentHover;
    COLORREF buttonBg;
    COLORREF buttonHover;
    COLORREF buttonBorder;
    COLORREF fieldBg;
    COLORREF fieldBorder;
    COLORREF hover;        // icon-button hover
    COLORREF danger;       // close-button hover
    int radius;
    int pad;
    bool dark;
    bool flatClick;        // classic look: 1px border, square corners
};

extern Theme g_theme;
void applyTheme(int style);

// ------------------------------------------------------------------- fonts

struct Fonts {
    HFONT title;    // window titles
    HFONT body;     // normal UI text
    HFONT hint;     // hints, status ('small' is a macro in rpcndr.h)
    HFONT bold;     // buttons, labels with emphasis
};

extern Fonts g_fonts;
// COM is initialised on first use (WIC, folder picker, dialogs), not at startup:
// starting with OleInitialize would load ole32/combase and keep them resident.
bool ensureCom();
void releaseCom();

// Fonts are created on first window creation, not at startup.
void ensureFonts();
void destroyFonts();

// ------------------------------------------------------------------ drawing

// Two rectangles in the same coordinate space.
inline bool rectsOverlap(const RECT& a, const RECT& b) {
    return a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom;
}

// Rounded rectangle drawn with GDI. CLR_NONE as fill = hollow, as stroke = no border.
void drawRoundRect(HDC hdc, const RECT& rc, int radius, COLORREF fill, COLORREF stroke,
                   int strokeWidth);
void drawTextGdi(HDC hdc, HFONT font, COLORREF color, const wchar_t* text, RECT rect,
                 UINT format);
int  textWidth(HDC hdc, HFONT font, const wchar_t* text);
void paintWindowBackdrop(HDC hdc, const RECT& client, bool rounded);

// Creates a double buffered memory DC sized to 'client' and copies back on end.
struct PaintBuffer {
    HDC dc;
    HBITMAP bitmap;
    HGDIOBJ previousBitmap;
};

PaintBuffer beginPaintBuffer(HWND hwnd, const RECT& client);
void endPaintBuffer(HWND hwnd, PaintBuffer& buffer);

// Double buffering is only worth its memory while the user is dragging the window.
void setPaintBuffered(bool buffered);
bool paintIsBuffered();

// ------------------------------------------------------------------ qr image

// Paints the symbol into 'area' (a 4 module quiet zone is added inside the modules).
void drawQrSymbol(HDC hdc, const RECT& area, const qr::Symbol& symbol, int padding);

// Shows the save dialog in a helper process (same exe, --pick-save) so the shell
// extensions it loads never stay resident in this one.
bool pickSavePathInChild(const std::wstring& initialDir, const std::wstring& suggestedName,
                         std::wstring& chosenPath);

// Renders the symbol into a 32bpp DIB and writes it as PNG through WIC.
bool saveQrPng(const qr::Symbol& symbol, int pixelSize, const std::wstring& path);

// Renders the symbol into a 32bpp DIB and puts it on the clipboard (CF_DIB + CF_BITMAP).
bool copyQrToClipboard(HWND owner, const qr::Symbol& symbol, int pixelSize);

std::wstring defaultFileNameFor(const std::wstring& content);

// ------------------------------------------------------------------- hotkey

std::wstring formatHotkey(UINT mods, UINT vk);
UINT normalizeHotkeyMods(UINT mods);
bool registerAppHotkey(UINT mods, UINT vk);
void unregisterAppHotkey();

// ------------------------------------------------------------------ windows

HWND createPopupWindow();
void showPopup();
void hidePopup();
void refreshPopupContent(bool pasteFromClipboard);
void popupSetStatus(const std::wstring& text, bool isError);

HWND createSettingsWindow();
void showSettingsWindow();
void settingsChangedExternally();

void rebuildTrayIcon();
void showTrayMenu();
void restartApplication();
void showAbout();
void quitApp();

// Draws an ownerdraw button from its declaration; shared by all windows.
enum class BtnKind {
    Primary,      // accent filled call to action
    Secondary,    // neutral button with border
    IconClose,    // 30x30 glyph button, red on hover
    IconSliders,  // 30x30 glyph button, neutral hover
    Toggle,       // switch; label drawn to the left by the parent
    Chip          // segmented option button
};

struct BtnSpec {
    int id;
    BtnKind kind;
    const wchar_t* text;
};

void drawButtonSpec(const DRAWITEMSTRUCT* dis, const BtnSpec& spec, bool checkedState);
void drawGlyphClose(HDC hdc, const RECT& rc, COLORREF color);
void drawGlyphSliders(HDC hdc, const RECT& rc, COLORREF color);
void drawToggleTrack(HDC hdc, const RECT& rc, bool on, bool hot);
void drawChip(HDC hdc, const RECT& rc, const wchar_t* text, bool selected, bool hot,
              bool disabled);

// Rounded window shape clipped by us (no DWM dependency) + the OS icon font.
void applyWindowShape(HWND hwnd);
bool hasSystemIconFont();
void drawSystemGlyph(HDC hdc, const RECT& rc, wchar_t glyph, COLORREF color);
COLORREF systemAccentColor();
COLORREF contrastingTextColor(COLORREF background);
void paintFieldFrame(HWND hwnd, HDC hdc, const RECT& rc, int radius);

void ensureDirectory(const std::wstring& path);

// Persistent button hover tracking for BS_OWNERDRAW buttons.
void installButtonSubclass(HWND button);
bool buttonIsHot(HWND button);
