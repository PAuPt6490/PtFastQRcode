#include "app.h"

#include <uxtheme.h>
#include <vssym32.h>

#include <algorithm>

#pragma comment(lib, "uxtheme.lib")

namespace {

COLORREF shade(COLORREF color, float factor) {
    int r = std::min(255, (int)(GetRValue(color) * factor));
    int g = std::min(255, (int)(GetGValue(color) * factor));
    int b = std::min(255, (int)(GetBValue(color) * factor));
    return RGB(r, g, b);
}

// Classic style: let the OS visual-styles engine paint its own controls (official API).
// Returns true when the button has been fully painted.
bool drawNativeButton(const DRAWITEMSTRUCT* dis, const BtnSpec& spec, bool checked) {
    if (spec.kind == BtnKind::IconClose || spec.kind == BtnKind::IconSliders) return false;

    HWND hwnd = dis->hwndItem;
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;
    const bool disabled = (dis->itemState & ODS_DISABLED) != 0;
    const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
    const bool hot = buttonIsHot(hwnd) && !disabled;

    HTHEME theme = OpenThemeData(hwnd, L"BUTTON");
    if (!theme) return false;

    if (spec.kind == BtnKind::Toggle) {
        int state;
        if (disabled)
            state = checked ? CBS_CHECKEDDISABLED : CBS_UNCHECKEDDISABLED;
        else
            state = checked ? (hot ? CBS_CHECKEDHOT : CBS_CHECKEDNORMAL)
                            : (hot ? CBS_UNCHECKEDHOT : CBS_UNCHECKEDNORMAL);

        SIZE size = {S(16), S(16)};
        GetThemePartSize(theme, hdc, BP_CHECKBOX, state, nullptr, TS_DRAW, &size);
        RECT box = {rc.left + (rc.right - rc.left - size.cx) / 2,
                    rc.top + (rc.bottom - rc.top - size.cy) / 2, 0, 0};
        box.right = box.left + size.cx;
        box.bottom = box.top + size.cy;
        DrawThemeBackground(theme, hdc, BP_CHECKBOX, state, &box, nullptr);
        CloseThemeData(theme);
        return true;
    }

    int state = PBS_NORMAL;
    if (disabled)
        state = PBS_DISABLED;
    else if (pressed || (spec.kind == BtnKind::Chip && checked))
        state = PBS_PRESSED;
    else if (hot)
        state = PBS_HOT;
    else if (spec.kind == BtnKind::Primary)
        state = PBS_DEFAULTED;

    DrawThemeBackground(theme, hdc, BP_PUSHBUTTON, state, &rc, nullptr);
    if (spec.text && spec.text[0])
        DrawThemeText(theme, hdc, BP_PUSHBUTTON, state, spec.text, -1,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE, 0, &rc);
    CloseThemeData(theme);
    return true;
}

}  // namespace

void drawGlyphClose(HDC hdc, const RECT& rc, COLORREF color) {
    if (hasSystemIconFont()) {
        drawSystemGlyph(hdc, rc, 0xE8BB, color);  // Segoe Fluent/MDL2 "ChromeClose"
        return;
    }
    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    float cx = (rc.left + rc.right) / 2.0f;
    float cy = (rc.top + rc.bottom) / 2.0f;
    float d = (float)S(5);
    Gdiplus::Pen pen(toGdiColor(color), (float)S(1.8));
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    g.DrawLine(&pen, cx - d, cy - d, cx + d, cy + d);
    g.DrawLine(&pen, cx + d, cy - d, cx - d, cy + d);
}

void drawGlyphSliders(HDC hdc, const RECT& rc, COLORREF color) {
    if (hasSystemIconFont()) {
        drawSystemGlyph(hdc, rc, 0xE713, color);  // Segoe "Setting"
        return;
    }
    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    float cx = (rc.left + rc.right) / 2.0f;
    float cy = (rc.top + rc.bottom) / 2.0f;
    float half = (float)S(7);
    float gap = (float)S(5);
    Gdiplus::Pen pen(toGdiColor(color), (float)S(1.6));
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    Gdiplus::SolidBrush knob(toGdiColor(color));

    const float knobs[3] = {-0.45f, 0.35f, -0.1f};
    for (int i = 0; i < 3; ++i) {
        float y = cy + (i - 1) * gap;
        g.DrawLine(&pen, cx - half, y, cx + half, y);
        float kx = cx + knobs[i] * half;
        g.FillEllipse(&knob, kx - (float)S(1.8), y - (float)S(1.8), (float)S(3.6),
                      (float)S(3.6));
    }
}

void drawToggleTrack(HDC hdc, const RECT& rc, bool on, bool hot) {
    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    float width = (float)S(40);
    float height = (float)S(22);
    float left = rc.left + ((rc.right - rc.left) - width) * 0.5f;
    float top = rc.top + ((rc.bottom - rc.top) - height) * 0.5f;

    COLORREF track = on ? g_theme.accent : (g_theme.dark ? RGB(76, 76, 76) : RGB(196, 196, 196));
    if (!on && hot) track = shade(track, 1.08f);

    drawRoundedRect(g, Gdiplus::RectF(left, top, width, height), height * 0.5f,
                    toGdiColor(track), Gdiplus::Color(0, 0, 0, 0), 0.0f);

    float knobSize = height - (float)S(6);
    float knobX = on ? left + width - knobSize - (float)S(3) : left + (float)S(3);
    Gdiplus::SolidBrush knob(on ? toGdiColor(g_theme.accentText) : Gdiplus::Color(255, 255, 255, 255));
    g.FillEllipse(&knob, knobX, top + (float)S(3), knobSize, knobSize);
}

void drawChip(HDC hdc, const RECT& rc, const wchar_t* text, bool selected, bool hot) {
    COLORREF fill = selected ? g_theme.accent : (hot ? g_theme.buttonHover : g_theme.buttonBg);
    COLORREF border = selected ? g_theme.accent : g_theme.buttonBorder;
    COLORREF textColor = selected ? g_theme.accentText : g_theme.text;

    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    RECT r = rc;
    drawRoundedRect(g, Gdiplus::RectF((float)r.left + 0.5f, (float)r.top + 0.5f,
                                      (float)(r.right - r.left) - 1.0f,
                                      (float)(r.bottom - r.top) - 1.0f),
                    (float)std::max(3, g_theme.radius - 2), toGdiColor(fill), toGdiColor(border),
                    1.0f);

    RECT textRect = rc;
    drawTextGdi(hdc, g_fonts.bold, textColor, text, textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void drawButtonSpec(const DRAWITEMSTRUCT* dis, const BtnSpec& spec, bool checkedState) {
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;
    HWND hwnd = dis->hwndItem;
    const bool disabled = (dis->itemState & ODS_DISABLED) != 0;
    const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
    const bool hot = buttonIsHot(hwnd) && !disabled;

    // Child buttons paint their own background; match the window surface so corners blend.
    HBRUSH background = CreateSolidBrush(g_theme.bg);
    FillRect(hdc, &rc, background);
    DeleteObject(background);

    // Win32 classic delegates to the OS visual-styles engine.
    if (g_theme.flatClick && !g_theme.dark && drawNativeButton(dis, spec, checkedState)) return;

    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    switch (spec.kind) {
        case BtnKind::Primary: {
            COLORREF fill = g_theme.accent;
            if (disabled) fill = g_theme.buttonBg;
            else if (pressed) fill = shade(g_theme.accent, 0.88f);
            else if (hot) fill = g_theme.accentHover;
            drawRoundedRect(g, Gdiplus::RectF((float)rc.left, (float)rc.top,
                                              (float)(rc.right - rc.left),
                                              (float)(rc.bottom - rc.top)),
                            (float)g_theme.radius, toGdiColor(fill), Gdiplus::Color(0, 0, 0, 0),
                            0.0f);
            COLORREF textColor = disabled ? g_theme.textDim : g_theme.accentText;
            RECT textRect = rc;
            drawTextGdi(hdc, g_fonts.bold, textColor, spec.text, textRect,
                        DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            break;
        }
        case BtnKind::Secondary: {
            COLORREF fill = pressed ? shade(g_theme.buttonBg, 0.92f)
                                    : (hot ? g_theme.buttonHover : g_theme.buttonBg);
            drawRoundedRect(g, Gdiplus::RectF((float)rc.left + 0.5f, (float)rc.top + 0.5f,
                                              (float)(rc.right - rc.left) - 1.0f,
                                              (float)(rc.bottom - rc.top) - 1.0f),
                            (float)g_theme.radius, toGdiColor(fill), toGdiColor(g_theme.buttonBorder),
                            1.0f);
            RECT textRect = rc;
            drawTextGdi(hdc, g_fonts.bold, disabled ? g_theme.textDim : g_theme.text, spec.text,
                        textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            break;
        }
        case BtnKind::IconClose:
        case BtnKind::IconSliders: {
            bool danger = spec.kind == BtnKind::IconClose;
            if (hot) {
                COLORREF fill = danger ? g_theme.danger : g_theme.hover;
                drawRoundedRect(g, Gdiplus::RectF((float)rc.left, (float)rc.top,
                                                  (float)(rc.right - rc.left),
                                                  (float)(rc.bottom - rc.top)),
                                (float)std::max(4, g_theme.radius - 2), toGdiColor(fill),
                                Gdiplus::Color(0, 0, 0, 0), 0.0f);
            }
            COLORREF glyphColor = (hot && danger) ? RGB(255, 255, 255) : g_theme.textDim;
            if (hot && !danger) glyphColor = g_theme.text;
            if (danger)
                drawGlyphClose(hdc, rc, glyphColor);
            else
                drawGlyphSliders(hdc, rc, glyphColor);
            break;
        }
        case BtnKind::Toggle: {
            drawToggleTrack(hdc, rc, checkedState, hot);
            break;
        }
        case BtnKind::Chip: {
            drawChip(hdc, rc, spec.text, checkedState, hot);
            break;
        }
    }

    // Keyboard focus ring for enabled buttons.
    if ((dis->itemState & ODS_FOCUS) && !(dis->itemState & ODS_NOFOCUSRECT) && !disabled) {
        Gdiplus::Pen pen(toGdiColor(g_theme.accent), 1.0f);
        pen.SetDashStyle(Gdiplus::DashStyleDot);
        g.DrawRectangle(&pen, rc.left + 2, rc.top + 2, rc.right - rc.left - 5,
                        rc.bottom - rc.top - 5);
    }
}
