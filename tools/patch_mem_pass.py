# -*- coding: utf-8 -*-
"""Memory pass: drop the unused font, create fonts lazily, load the save dialog on demand."""
import io


def patch(path, pairs):
    text = io.open(path, encoding="utf-8-sig").read()
    for old, new in pairs:
        if old not in text:
            raise SystemExit("MISSING in %s:\n%s" % (path, old[:140]))
        text = text.replace(old, new, 1)
    io.open(path, "w", encoding="utf-8", newline="\n").write(text)
    print("patched", path.split("\\")[-1])


# ---------------------------------------------------------------- app.h
patch(r"C:\PtFastQRcode\src\app\app.h", [
    ("""struct Fonts {
    HFONT title;    // window titles
    HFONT body;     // normal UI text
    HFONT hint;     // hints, status ('small' is a macro in rpcndr.h)
    HFONT bold;     // buttons, labels with emphasis
    HFONT mono;     // (unused placeholder, kept for future)
};""",
     """struct Fonts {
    HFONT title;    // window titles
    HFONT body;     // normal UI text
    HFONT hint;     // hints, status ('small' is a macro in rpcndr.h)
    HFONT bold;     // buttons, labels with emphasis
};"""),
    ("""void createFonts();
void destroyFonts();""",
     """// Fonts are created on first window creation, not at startup.
void ensureFonts();
void destroyFonts();"""),
])

# ---------------------------------------------------------------- core.cpp
patch(r"C:\PtFastQRcode\src\app\core.cpp", [
    ("""    destroyFonts();
    createFonts();
}""",
     """    destroyFonts();   // recreated lazily by the next window
}"""),

    ("""void createFonts() {
    g_fonts.title = makeFont(S(16), FW_SEMIBOLD);
    g_fonts.body = makeFont(S(14), FW_NORMAL);
    g_fonts.hint = makeFont(S(12), FW_NORMAL);
    g_fonts.bold = makeFont(S(13), FW_SEMIBOLD);
    g_fonts.mono = makeFont(S(13), FW_NORMAL);
}""",
     """void ensureFonts() {
    if (g_fonts.title) return;
    g_fonts.title = makeFont(S(16), FW_SEMIBOLD);
    g_fonts.body = makeFont(S(14), FW_NORMAL);
    g_fonts.hint = makeFont(S(12), FW_NORMAL);
    g_fonts.bold = makeFont(S(13), FW_SEMIBOLD);
}"""),

    ("""void destroyFonts() {
    HFONT fonts[] = {g_fonts.title, g_fonts.body, g_fonts.hint, g_fonts.bold, g_fonts.mono};
    for (HFONT font : fonts) {
        if (font) DeleteObject(font);
    }
    g_fonts.title = g_fonts.body = g_fonts.hint = g_fonts.bold = g_fonts.mono = nullptr;""",
     """void destroyFonts() {
    HFONT fonts[] = {g_fonts.title, g_fonts.body, g_fonts.hint, g_fonts.bold};
    for (HFONT font : fonts) {
        if (font) DeleteObject(font);
    }
    g_fonts.title = g_fonts.body = g_fonts.hint = g_fonts.bold = nullptr;"""),
])

# ---------------------------------------------------------------- popup.cpp
patch(r"C:\PtFastQRcode\src\app\popup.cpp", [
    ("""    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kWndClassPopup, kAppName,""",
     """    ensureFonts();
    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kWndClassPopup, kAppName,"""),

    ("""    st->suppressDeactivate = true;
    BOOL accepted = GetSaveFileNameW(&dialog);
    st->suppressDeactivate = false;""",
     """    st->suppressDeactivate = true;
    const BOOL accepted = pickSavePath(&dialog);
    st->suppressDeactivate = false;"""),

    ("""std::wstring baseName(const std::wstring& path) {""",
     """// comdlg32 is loaded on first save instead of at startup: one DLL less in the tray-only state.
typedef BOOL(WINAPI* GetSaveFileNameWFn)(LPOPENFILENAMEW);

BOOL pickSavePath(OPENFILENAMEW* dialog) {
    static GetSaveFileNameWFn resolve = nullptr;
    static bool resolved = false;
    if (!resolved) {
        resolved = true;
        HMODULE module = LoadLibraryW(L"comdlg32.dll");
        if (module) resolve = (GetSaveFileNameWFn)GetProcAddress(module, "GetSaveFileNameW");
    }
    return resolve && resolve(dialog);
}

std::wstring baseName(const std::wstring& path) {"""),
])

# ---------------------------------------------------------------- settings_win.cpp
patch(r"C:\PtFastQRcode\src\app\settings_win.cpp", [
    ("""    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, kWndClassSettings, L"设置",""",
     """    ensureFonts();
    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, kWndClassSettings, L"设置","""),
])

# ---------------------------------------------------------------- build.ps1
path = r"C:\PtFastQRcode\build.ps1"
raw = io.open(path, "rb").read()
text = raw.decode("utf-8-sig")
old = """    'kernel32.lib', 'user32.lib', 'gdi32.lib', 'shell32.lib', 'shlwapi.lib',
    'ole32.lib', 'comctl32.lib', 'comdlg32.lib', 'uuid.lib', 'advapi32.lib',
    'uxtheme.lib', 'windowscodecs.lib'"""
new = """    'kernel32.lib', 'user32.lib', 'gdi32.lib', 'shell32.lib', 'shlwapi.lib',
    'ole32.lib', 'comctl32.lib', 'uuid.lib', 'advapi32.lib', 'uxtheme.lib',
    'windowscodecs.lib'"""
assert old in text, "libs"
text = text.replace(old, new, 1)
io.open(path, "wb").write(b"\xef\xbb\xbf" + text.encode("utf-8"))
print("build.ps1: comdlg32 no longer imported")
