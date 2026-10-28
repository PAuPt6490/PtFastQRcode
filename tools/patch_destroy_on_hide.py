# -*- coding: utf-8 -*-
"""Popup is destroyed when hidden so the tray-only footprint is the only resident cost."""
import io


def patch(path, pairs):
    text = io.open(path, encoding="utf-8-sig").read()
    for old, new in pairs:
        if old not in text:
            raise SystemExit("MISSING in %s:\n%s" % (path, old[:160]))
        text = text.replace(old, new, 1)
    io.open(path, "w", encoding="utf-8", newline="\n").write(text)
    print("patched", path.split("\\")[-1])


# ---- app.h: one more private message ---------------------------------------
patch(r"C:\PtFastQRcode\src\app\app.h", [
    ("#define WM_APP_TRAY (WM_APP + 11)", "#define WM_APP_TRAY (WM_APP + 11)\n#define WM_APP_DESTROY (WM_APP + 12)"),
])

# ---- popup.cpp -------------------------------------------------------------
patch(r"C:\PtFastQRcode\src\app\popup.cpp", [
    ("""void hideWindow(HWND hwnd) {
    PopupState* st = getState(hwnd);
    if (st) {
        KillTimer(hwnd, TIMER_DEBOUNCE);
        KillTimer(hwnd, TIMER_STATUS);
        KillTimer(hwnd, TIMER_HIDE);
        st->message.clear();
        st->suppressDeactivate = false;
    }
    ShowWindow(hwnd, SW_HIDE);
}""",
     """// Hiding also destroys the window: a hidden popup would otherwise keep its edit control,
// fonts and layout strings resident, so returning to the tray never fell back fully.
// The window is hidden immediately (no flicker) and destroyed from the message loop.
void hideWindow(HWND hwnd) {
    PopupState* st = getState(hwnd);
    if (st) {
        KillTimer(hwnd, TIMER_DEBOUNCE);
        KillTimer(hwnd, TIMER_STATUS);
        KillTimer(hwnd, TIMER_HIDE);
        st->message.clear();
        st->suppressDeactivate = true;
    }
    ShowWindow(hwnd, SW_HIDE);
    PostMessageW(hwnd, WM_APP_DESTROY, 0, 0);
}"""),

    ("""        case WM_CLOSE:
            hideWindow(hwnd);
            return 0;""",
     """        case WM_CLOSE:
            hideWindow(hwnd);
            return 0;

        case WM_APP_DESTROY:
            DestroyWindow(hwnd);
            return 0;"""),
])
