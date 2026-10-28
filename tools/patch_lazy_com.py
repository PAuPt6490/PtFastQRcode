# -*- coding: utf-8 -*-
"""Background pass 1: no COM at startup, no shlwapi/comctl32 imports."""
import io


def patch(path, pairs):
    text = io.open(path, encoding="utf-8-sig").read()
    for old, new in pairs:
        if old not in text:
            raise SystemExit("MISSING in %s:\n%s" % (path, old[:160]))
        text = text.replace(old, new, 1)
    io.open(path, "w", encoding="utf-8", newline="\n").write(text)
    print("patched", path.split("\\")[-1])


# ---- app.h: COM helpers ------------------------------------------------------
patch(r"C:\PtFastQRcode\src\app\app.h", [
    ("""// Fonts are created on first window creation, not at startup.""",
     """// COM is initialised on first use (WIC, folder picker, dialogs), not at startup:
// starting with OleInitialize would load ole32/combase and keep them resident.
bool ensureCom();
void releaseCom();

// Fonts are created on first window creation, not at startup."""),
])

# ---- core.cpp: implement the helpers ----------------------------------------
patch(r"C:\PtFastQRcode\src\app\core.cpp", [
    ("""// ------------------------------------------------------------------ fonts""",
     """// ------------------------------------------------------------------ COM

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

// ------------------------------------------------------------------ fonts"""),
])

# ---- main.cpp: drop OleInitialize/OleUninitialize ----------------------------
patch(r"C:\PtFastQRcode\src\app\main.cpp", [
    ("""    OleInitialize(nullptr);""",
     """    // COM stays uninitialised until something needs it (see ensureCom)."""),
    ("""        if (mutex) CloseHandle(mutex);
        OleUninitialize();""",
     """        if (mutex) CloseHandle(mutex);
        releaseCom();"""),
    ("""    destroyFonts();
    OleUninitialize();""",
     """    destroyFonts();
    releaseCom();"""),
])

# ---- qrimage.cpp: WIC needs COM --------------------------------------------
patch(r"C:\PtFastQRcode\src\app\qrimage.cpp", [
    ("""bool writePng(const Surface& surface, const std::wstring& path) {
    IWICImagingFactory* factory = nullptr;""",
     """bool writePng(const Surface& surface, const std::wstring& path) {
    if (!ensureCom()) return false;
    IWICImagingFactory* factory = nullptr;"""),
])

# ---- settings_win.cpp: folder picker needs COM ------------------------------
text = io.open(r"C:\PtFastQRcode\src\app\settings_win.cpp", encoding="utf-8-sig").read()
marker = "IFileOpenDialog* dialog = nullptr;"
if marker not in text:
    raise SystemExit("MISSING folder picker")
text = text.replace(marker, "if (!ensureCom()) return;\n    " + marker, 1)
io.open(r"C:\PtFastQRcode\src\app\settings_win.cpp", "w", encoding="utf-8", newline="\n").write(text)
print("patched settings_win.cpp")

# ---- build.ps1: drop shlwapi ------------------------------------------------
path = r"C:\PtFastQRcode\build.ps1"
raw = io.open(path, "rb").read()
text = raw.decode("utf-8-sig")
old = """    'kernel32.lib', 'user32.lib', 'gdi32.lib', 'shell32.lib', 'shlwapi.lib',
    'ole32.lib', 'comctl32.lib', 'uuid.lib', 'advapi32.lib', 'uxtheme.lib',
    'windowscodecs.lib'"""
new = """    'kernel32.lib', 'user32.lib', 'gdi32.lib', 'shell32.lib',
    'ole32.lib', 'comctl32.lib', 'uuid.lib', 'advapi32.lib', 'uxtheme.lib',
    'windowscodecs.lib'"""
assert old in text
io.open(path, "wb").write(b"\xef\xbb\xbf" + text.replace(old, new, 1).encode("utf-8"))
print("build.ps1: shlwapi removed")
