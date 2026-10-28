# -*- coding: utf-8 -*-
"""Replace the GDI+ drawing helpers in core.cpp with plain GDI."""
import io

path = r"C:\PtFastQRcode\src\app\core.cpp"
text = io.open(path, encoding="utf-8-sig").read()


def sub(old, new, label):
    global text
    if old not in text:
        raise SystemExit("MISSING: " + label)
    text = text.replace(old, new, 1)


sub("""Gdiplus::Color toGdiColor(COLORREF color) {
    return Gdiplus::Color(255, GetRValue(color), GetGValue(color), GetBValue(color));
}

Gdiplus::Color toGdiColor(COLORREF color, BYTE alpha) {
    return Gdiplus::Color(alpha, GetRValue(color), GetGValue(color), GetBValue(color));
}

void drawRoundedRect(Gdiplus::Graphics& g, const Gdiplus::RectF& rect, float radius,
                     const Gdiplus::Color& fill, const Gdiplus::Color& stroke, float strokeWidth) {
    // A zero stroke width only means "no border": rounded corners must still be rounded.
    const bool drawStroke = strokeWidth > 0.0f && stroke.GetA() > 0;
    if (radius <= 0.5f) {
        // Square corners (classic theme): plain rectangles stay pixel crisp.
        Gdiplus::SolidBrush brush(fill);
        g.FillRectangle(&brush, rect);
        if (drawStroke) {
            Gdiplus::Pen pen(stroke, strokeWidth);
            g.DrawRectangle(&pen, rect.X + strokeWidth / 2, rect.Y + strokeWidth / 2,
                            rect.Width - strokeWidth, rect.Height - strokeWidth);
        }
        return;
    }

    float r = radius * 2.0f;
    if (r > rect.Width) r = rect.Width;
    if (r > rect.Height) r = rect.Height;
    Gdiplus::GraphicsPath path;
    path.AddArc(rect.X, rect.Y, r, r, 180.0f, 90.0f);
    path.AddArc(rect.X + rect.Width - r, rect.Y, r, r, 270.0f, 90.0f);
    path.AddArc(rect.X + rect.Width - r, rect.Y + rect.Height - r, r, r, 0.0f, 90.0f);
    path.AddArc(rect.X, rect.Y + rect.Height - r, r, r, 90.0f, 90.0f);
    path.CloseFigure();

    Gdiplus::SolidBrush brush(fill);
    g.FillPath(&brush, &path);
    if (drawStroke) {
        Gdiplus::Pen pen(stroke, strokeWidth);
        g.DrawPath(&pen, &path);
    }
}""",
    """// Rounded rectangle through GDI. Pass CLR_NONE as fill for a hollow frame,
// CLR_NONE as stroke for a borderless fill.
void drawRoundRect(HDC hdc, const RECT& rc, int radius, COLORREF fill, COLORREF stroke,
                   int strokeWidth) {
    if (rc.right - rc.left <= 0 || rc.bottom - rc.top <= 0) return;

    const bool filled = fill != CLR_NONE;
    const bool stroked = stroke != CLR_NONE && strokeWidth > 0;

    HBRUSH brush = filled ? CreateSolidBrush(fill) : (HBRUSH)GetStockObject(NULL_BRUSH);
    HPEN pen = stroked ? CreatePen(PS_SOLID, strokeWidth, stroke) : (HPEN)GetStockObject(NULL_PEN);
    HGDIOBJ oldBrush = SelectObject(hdc, brush);
    HGDIOBJ oldPen = SelectObject(hdc, pen);

    // RoundRect's last two parameters are the corner ellipse size; 0 gives square corners.
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, radius * 2, radius * 2);

    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    if (stroked) DeleteObject(pen);
    if (filled) DeleteObject(brush);
}""", "drawRoundRect")

sub("""void paintWindowBackdrop(HDC hdc, const RECT& client, bool rounded) {
    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
    Gdiplus::RectF rect(0.0f, 0.0f, (float)client.right, (float)client.bottom);
    Gdiplus::SolidBrush background(toGdiColor(g_theme.bg));
    g.FillRectangle(&background, rect);
    g.SetCompositingMode(Gdiplus::CompositingModeSourceOver);

    float radius = rounded ? (float)g_theme.radius : 0.0f;
    Gdiplus::Color border = toGdiColor(g_theme.cardBorder);
    drawRoundedRect(g, Gdiplus::RectF(0.5f, 0.5f, rect.Width - 1.0f, rect.Height - 1.0f),
                    radius, Gdiplus::Color(0, 0, 0, 0), border, 1.0f);
}""",
    """void paintWindowBackdrop(HDC hdc, const RECT& client, bool rounded) {
    HBRUSH background = CreateSolidBrush(g_theme.bg);
    FillRect(hdc, &client, background);
    DeleteObject(background);

    if (!rounded) return;
    drawRoundRect(hdc, client, g_theme.radius, CLR_NONE, g_theme.cardBorder, 1);
}""", "paintWindowBackdrop")

io.open(path, "w", encoding="utf-8", newline="\n").write(text)
print("core.cpp drawing converted to GDI")
