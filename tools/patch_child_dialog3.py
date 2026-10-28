# -*- coding: utf-8 -*-
"""Resolve comdlg32 dynamically so the tray process never imports it."""
import io

MAIN = r"C:\PtFastQRcode\src\app\main.cpp"
text = io.open(MAIN, encoding="utf-8-sig").read()


def sub(old, new, label):
    global text
    if old not in text:
        raise SystemExit("MISSING: " + label)
    text = text.replace(old, new, 1)


sub('#include "app.h"', '#include "app.h"\n\n#include <commdlg.h>   // types only: comdlg32 is resolved at run time', "include")

sub("""// Reads one quoted argument starting at cursor; advances the cursor past it.""",
    """// comdlg32 is resolved on demand: importing it would load the dialog stack into the
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

// Reads one quoted argument starting at cursor; advances the cursor past it.""", "resolver")

sub("""    const BOOL accepted = GetSaveFileNameW(&dialog);""",
    """    const GetSaveFileNameWFn showDialog = resolveSaveDialog();
    const BOOL accepted = showDialog ? showDialog(&dialog) : FALSE;""", "call")

io.open(MAIN, "w", encoding="utf-8", newline="\n").write(text)
print("main.cpp: comdlg32 resolved at run time")
