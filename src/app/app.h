// FastQRcode - shared application declarations.
#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objidl.h>   // GDI+ headers need IStream from here when WIN32_LEAN_AND_MEAN is set
#include <gdiplus.h>

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
    int uiStyle;    // 0 = Win32 classic, 1 = WinUI 3
    int themeMode;  // 0 = follow system, 1 = light, 2 = dark
    int popupWidth;   // remembered popup size in design pixels
    int popupHeight;
    std::wstring saveDir;
};

extern Settings g_settings;

bool systemPrefersDark();
bool themeIsDark();

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
    HFONT mono;     // (unused placeholder, kept for future)
};

extern Fonts g_fonts;
void createFonts();
void destroyFonts();

// ------------------------------------------------------------------ drawing

void drawRoundedRect(Gdiplus::Graphics& g, const Gdiplus::RectF& rect, float radius,
                     const Gdiplus::Color& fill, const Gdiplus::Color& stroke, float strokeWidth);
void drawTextGdi(HDC hdc, HFONT font, COLORREF color, const wchar_t* text, RECT rect,
                 UINT format);
int  textWidth(HDC hdc, HFONT font, const wchar_t* text);
Gdiplus::Color toGdiColor(COLORREF color);
Gdiplus::Color toGdiColor(COLORREF color, BYTE alpha);
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

// Renders the symbol into a white PNG-ready bitmap with a 4 module quiet zone.
Gdiplus::Bitmap* renderQrBitmap(const qr::Symbol& symbol, int pixelSize);

// Paints the symbol directly into 'area' (no bitmap kept in memory).
void drawQrSymbol(HDC hdc, const RECT& area, const qr::Symbol& symbol);

bool saveBitmapAsPng(Gdiplus::Bitmap* bitmap, const std::wstring& path);
bool copyBitmapToClipboard(HWND owner, Gdiplus::Bitmap* bitmap);

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
void drawChip(HDC hdc, const RECT& rc, const wchar_t* text, bool selected, bool hot);

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
