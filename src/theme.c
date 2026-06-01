#include "theme.h"

static HFONT g_font_display, g_font_title, g_font_mono, g_font_small, g_font_small_bold;
static HBRUSH g_brush_bg, g_brush_surface, g_brush_primary;

void theme_init(void) {
    g_font_display = CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
        0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_font_title = CreateFontW(-15, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
        0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_font_mono = CreateFontW(-11, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
        0, 0, CLEARTYPE_QUALITY, 0, L"Consolas");
    g_font_small = CreateFontW(-11, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
        0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_font_small_bold = CreateFontW(-11, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
        0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");

    g_brush_bg = CreateSolidBrush(CLR_BG_DARK);
    g_brush_surface = CreateSolidBrush(CLR_SURFACE_DARK);
    g_brush_primary = CreateSolidBrush(CLR_PRIMARY);
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

void theme_draw_rounded_rect(HDC hdc, RECT *rc, int radius, HBRUSH fill, HPEN border) {
    HRGN rgn = CreateRoundRectRgn(rc->left, rc->top, rc->right, rc->bottom, radius*2, radius*2);
    if (fill) FillRgn(hdc, rgn, fill);
    if (border) FrameRgn(hdc, rgn, (HBRUSH)GetStockObject(NULL_BRUSH), 1, 1);
    if (border) {
        HPEN old = (HPEN)SelectObject(hdc, border);
        RoundRect(hdc, rc->left, rc->top, rc->right, rc->bottom, radius*2, radius*2);
        SelectObject(hdc, old);
    }
    DeleteObject(rgn);
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

/* Whole-archive color: a YTD is fine up to the 16 MiB streaming/green limit,
 * so stay green through ~16 MiB, then warn as it grows past it. */
COLORREF theme_archive_size_color(double mib) {
    if (mib <= 12.0) return CLR_SIZE_GREEN;
    if (mib <= 16.0) return CLR_SIZE_LGREEN;   /* at/under the limit: still OK */
    if (mib <= 20.0) return CLR_SIZE_YELLOW;
    if (mib <= 24.0) return CLR_SIZE_DYELLOW;
    if (mib <= 32.0) return CLR_SIZE_ORANGE;
    if (mib <= 48.0) return CLR_SIZE_DORANGE;
    if (mib <  64.0) return CLR_SIZE_RED;
    return CLR_SIZE_DRED;
}
