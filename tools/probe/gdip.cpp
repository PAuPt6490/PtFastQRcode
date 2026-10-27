// Probe 2: same minimal window, but painting a handful of GDI+ rounded rectangles,
// to measure what the GDI+ pipeline costs on its own.
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

using namespace Gdiplus;

static void DrawRounded(Graphics& g, const RectF& rect, float radius, Color fill) {
    float r = radius * 2.0f;
    GraphicsPath path;
    path.AddArc(rect.X, rect.Y, r, r, 180.0f, 90.0f);
    path.AddArc(rect.X + rect.Width - r, rect.Y, r, r, 270.0f, 90.0f);
    path.AddArc(rect.X + rect.Width - r, rect.Y + rect.Height - r, r, r, 0.0f, 90.0f);
    path.AddArc(rect.X, rect.Y + rect.Height - r, r, r, 90.0f, 90.0f);
    path.CloseFigure();
    SolidBrush brush(fill);
    g.FillPath(&brush, &path);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);

            Graphics g(dc);
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            SolidBrush background(Color(255, 243, 243, 243));
            g.FillRectangle(&background, 0, 0, rc.right, rc.bottom);

            for (int i = 0; i < 5; ++i) {
                DrawRounded(g, RectF(10.0f, 100.0f + i * 40.0f, 120.0f, 30.0f), 8.0f,
                            Color(255, 251, 251, 251));
            }
            DrawRounded(g, RectF(10.0f, 20.0f, 380.0f, 300.0f), 8.0f, Color(255, 255, 255, 255));

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
    GdiplusStartupInput input;
    ULONG_PTR token = 0;
    GdiplusStartup(&token, &input, nullptr);

    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"ProbeWindowGdip";
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, L"ProbeWindowGdip", L"probe", WS_OVERLAPPEDWINDOW, 100, 100, 600,
                                705, nullptr, nullptr, instance, nullptr);
    CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE, 10, 10, 300, 60, hwnd,
                    nullptr, instance, nullptr);
    for (int i = 0; i < 5; ++i) {
        CreateWindowExW(0, L"BUTTON", L"btn", WS_CHILD | WS_VISIBLE, 10, 100 + i * 40, 120, 30,
                        hwnd, nullptr, instance, nullptr);
    }
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    GdiplusShutdown(token);
    return 0;
}
