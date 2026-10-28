#include "app.h"

#include <uxtheme.h>
#include <vssym32.h>

#include <algorithm>

#pragma comment(lib, "uxtheme.lib")

namespace {

COLORREF blendColors(COLORREF foreground, COLORREF background, float amount) {
    const auto mix = [amount](int a, int b) {
        return (int)(b + (a - b) * amount + 0.5f);
    };
    return RGB(mix(GetRValue(foreground), GetRValue(background)),
               mix(GetGValue(foreground), GetGValue(background)),
               mix(GetBValue(foreground), GetBValue(background)));
}

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
    const int cx = (rc.left + rc.right) / 2;
    const int cy = (rc.top + rc.bottom) / 2;
    const int d = S(5);
    HPEN pen = CreatePen(PS_SOLID, S(2), color);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    MoveToEx(hdc, cx - d, cy - d, nullptr);
    LineTo(hdc, cx + d + 1, cy + d + 1);
    MoveToEx(hdc, cx + d, cy - d, nullptr);
    LineTo(hdc, cx - d - 1, cy + d + 1);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

void drawGlyphSliders(HDC hdc, const RECT& rc, COLORREF color) {
    if (hasSystemIconFont()) {
        drawSystemGlyph(hdc, rc, 0xE713, color);  // Segoe "Setting"
        return;
    }
    const int cx = (rc.left + rc.right) / 2;
    const int cy = (rc.top + rc.bottom) / 2;
    const int half = S(7);
    const int gap = S(5);
    HPEN pen = CreatePen(PS_SOLID, S(2), color);
    HBRUSH brush = CreateSolidBrush(color);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBrush = SelectObject(hdc, brush);

    const int knobs[3] = {-4, 3, -1};
    for (int i = 0; i < 3; ++i) {
        const int y = cy + (i - 1) * gap;
        MoveToEx(hdc, cx - half, y, nullptr);
        LineTo(hdc, cx + half + 1, y);
        const int kx = cx + knobs[i] * half / 10;
        Ellipse(hdc, kx - S(2), y - S(2), kx + S(2), y + S(2));
    }
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

void drawToggleTrack(HDC hdc, const RECT& rc, bool on, bool hot) {
    const int width = S(40);
    const int height = S(22);
    const int left = rc.left + (rc.right - rc.left - width) / 2;
    const int top = rc.top + (rc.bottom - rc.top - height) / 2;

    COLORREF track = on ? g_theme.accent : (g_theme.dark ? RGB(76, 76, 76) : RGB(196, 196, 196));
    if (!on && hot) track = shade(track, 1.08f);

    drawRoundRect(hdc, RECT{left, top, left + width, top + height}, height / 2, track, CLR_NONE, 0);

    const int knobSize = height - S(6);
    const int knobX = on ? left + width - knobSize - S(3) : left + S(3);
    const COLORREF knobColor = on ? g_theme.accentText : RGB(255, 255, 255);
    HBRUSH knob = CreateSolidBrush(knobColor);
    HGDIOBJ oldBrush = SelectObject(hdc, knob);
    HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(NULL_PEN));
    Ellipse(hdc, knobX, top + S(3), knobX + knobSize + 1, top + S(3) + knobSize + 1);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(knob);
}

void drawChip(HDC hdc, const RECT& rc, const wchar_t* text, bool selected, bool hot,
              bool disabled) {
    COLORREF fill = selected ? g_theme.accent : (hot ? g_theme.buttonHover : g_theme.buttonBg);
    COLORREF border = selected ? g_theme.accent : g_theme.buttonBorder;
    COLORREF textColor = selected ? g_theme.accentText : g_theme.text;
    if (disabled) {
        // Greyed out: the selected one keeps a muted accent so the state stays readable.
        fill = selected ? blendColors(g_theme.accent, g_theme.bg, 0.35f) : g_theme.bg;
        border = selected ? blendColors(g_theme.accent, g_theme.bg, 0.35f) : g_theme.buttonBorder;
        textColor = g_theme.textDim;
    }

    drawRoundRect(hdc, rc, std::max(3, g_theme.radius - 2), fill, border, 1);

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

    switch (spec.kind) {
        case BtnKind::Primary: {
            COLORREF fill = g_theme.accent;
            if (disabled) fill = g_theme.buttonBg;
            else if (pressed) fill = shade(g_theme.accent, 0.88f);
            else if (hot) fill = g_theme.accentHover;
            drawRoundRect(hdc, rc, g_theme.radius, fill, CLR_NONE, 0);
            COLORREF textColor = disabled ? g_theme.textDim : g_theme.accentText;
            RECT textRect = rc;
            drawTextGdi(hdc, g_fonts.bold, textColor, spec.text, textRect,
                        DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            break;
        }
        case BtnKind::Secondary: {
            COLORREF fill = pressed ? shade(g_theme.buttonBg, 0.92f)
                                    : (hot ? g_theme.buttonHover : g_theme.buttonBg);
            drawRoundRect(hdc, rc, g_theme.radius, fill, g_theme.buttonBorder, 1);
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
                drawRoundRect(hdc, rc, std::max(4, g_theme.radius - 2), fill, CLR_NONE, 0);
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
            drawChip(hdc, rc, spec.text, checkedState, hot, disabled);
            break;
        }
    }

    // Keyboard focus ring for enabled buttons.
    if ((dis->itemState & ODS_FOCUS) && !(dis->itemState & ODS_NOFOCUSRECT) && !disabled) {
        RECT ring = {rc.left + 2, rc.top + 2, rc.right - 2, rc.bottom - 2};
        DrawFocusRect(hdc, &ring);
    }
}
