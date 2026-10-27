// Probe: how little private memory can a plain Win32 window with a few child controls use?
// No GDI+, no common controls, static CRT, same 100x100 design as the real app.
#include <windows.h>

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            HBRUSH brush = CreateSolidBrush(RGB(240, 240, 240));
            FillRect(dc, &rc, brush);
            DeleteObject(brush);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(20, 20, 20));
            DrawTextW(dc, L"probe", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"ProbeWindow";
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, L"ProbeWindow", L"probe", WS_OVERLAPPEDWINDOW, 100, 100, 600,
                                705, nullptr, nullptr, instance, nullptr);
    HWND edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE, 10, 10, 300,
                                60, hwnd, nullptr, instance, nullptr);
    for (int i = 0; i < 5; ++i) {
        CreateWindowExW(0, L"BUTTON", L"btn", WS_CHILD | WS_VISIBLE, 10, 100 + i * 40, 120, 30,
                        hwnd, nullptr, instance, nullptr);
    }
    (void)edit;
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
