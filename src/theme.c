#include "theme.h"

static HFONT g_font_display, g_font_title, g_font_mono, g_font_small, g_font_small_bold;
static HBRUSH g_brush_bg, g_brush_surface, g_brush_primary;

void theme_init(void) {
    /* Visual Studio 2012 uses Segoe UI for chrome and Consolas for code. The
     * sizes mirror VS2012: 12px UI text, ~9pt small chrome, 13px editor. */
    g_font_display = CreateFontW(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
        0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_font_title = CreateFontW(-15, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET,
        0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_font_mono = CreateFontW(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
        0, 0, CLEARTYPE_QUALITY, 0, L"Consolas");
    g_font_small = CreateFontW(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
        0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_font_small_bold = CreateFontW(-12, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET,
        0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");

    g_brush_bg = CreateSolidBrush(CLR_VS_EDITOR);
    g_brush_surface = CreateSolidBrush(CLR_VS_SIDEBAR);
    g_brush_primary = CreateSolidBrush(CLR_VS_ACCENT);
}

void theme_cleanup(void) {
    DeleteObject(g_font_display);
    DeleteObject(g_font_title);
    DeleteObject(g_font_mono);
    DeleteObject(g_font_small);
    DeleteObject(g_font_small_bold);
    DeleteObject(g_brush_bg);
    DeleteObject(g_brush_surface);
    DeleteObject(g_brush_primary);
}

HFONT theme_font_display(void) { return g_font_display; }
HFONT theme_font_title(void) { return g_font_title; }
HFONT theme_font_mono(void) { return g_font_mono; }
HFONT theme_font_small(void) { return g_font_small; }
HFONT theme_font_small_bold(void) { return g_font_small_bold; }

HBRUSH theme_brush_bg(void) { return g_brush_bg; }
HBRUSH theme_brush_surface(void) { return g_brush_surface; }
HBRUSH theme_brush_primary(void) { return g_brush_primary; }

void theme_fill_rect(HDC hdc, RECT *rc, COLORREF fill) {
    HBRUSH b = CreateSolidBrush(fill);
    FillRect(hdc, rc, b);
    DeleteObject(b);
}

/* Flat VS2012 element: solid fill + crisp 1px square border (no rounding). */
void theme_flat_rect(HDC hdc, RECT *rc, COLORREF fill, COLORREF border) {
    HBRUSH b = CreateSolidBrush(fill);
    FillRect(hdc, rc, b);
    DeleteObject(b);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, rc->left, rc->top, rc->right, rc->bottom);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(pen);
}

/* VS2012 has no rounded corners — keep the signature but draw flat so any
 * remaining callers still match the aesthetic. */
void theme_draw_rounded_rect(HDC hdc, RECT *rc, int radius, HBRUSH fill, HPEN border) {
    (void)radius;
    if (fill) FillRect(hdc, rc, fill);
    if (border) {
        HPEN old = (HPEN)SelectObject(hdc, border);
        HBRUSH ob = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, rc->left, rc->top, rc->right, rc->bottom);
        SelectObject(hdc, old);
        SelectObject(hdc, ob);
    }
}

COLORREF theme_size_color(double mib) {
    if (mib <= 0.70) return CLR_SIZE_GREEN;
    if (mib <= 1.0)  return CLR_SIZE_LGREEN;
    if (mib <= 1.5)  return CLR_SIZE_YELLOW;
    if (mib <= 2.0)  return CLR_SIZE_DYELLOW;
    if (mib <= 2.5)  return CLR_SIZE_ORANGE;
    if (mib <= 3.5)  return CLR_SIZE_DORANGE;
    if (mib < 4.5)   return CLR_SIZE_RED;
    return CLR_SIZE_DRED;
}

COLORREF theme_archive_size_color(double mib) {
    if (mib <= 12.0) return CLR_SIZE_GREEN;
    if (mib <= 16.0) return CLR_SIZE_LGREEN;
    if (mib <= 20.0) return CLR_SIZE_YELLOW;
    if (mib <= 24.0) return CLR_SIZE_DYELLOW;
    if (mib <= 32.0) return CLR_SIZE_ORANGE;
    if (mib <= 48.0) return CLR_SIZE_DORANGE;
    if (mib <  64.0) return CLR_SIZE_RED;
    return CLR_SIZE_DRED;
}
