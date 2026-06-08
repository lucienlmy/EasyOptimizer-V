#ifndef EO_THEME_H
#define EO_THEME_H

#include <windows.h>

/* ──────────────────────────────────────────────────────────────────────────
 * Visual Studio 2012 Ultimate — exact visual style (LIGHT theme).
 * Palette mirrors UI/themes.css (.vs2012-app.theme-light). Flat chrome,
 * 1px panel borders, the signature #007ACC accent, Segoe UI text.
 * ────────────────────────────────────────────────────────────────────────── */

/* Core surfaces */
#define CLR_VS_MAIN       RGB(0xEE, 0xEF, 0xF3)   /* background */
#define CLR_VS_SIDEBAR    RGB(0xEE, 0xEF, 0xF3)   /* toolbar */
#define CLR_VS_EDITOR     RGB(0xFF, 0xFF, 0xFF)   /* editor */
#define CLR_VS_MENU       RGB(0xEE, 0xEF, 0xF3)   /* menu */
#define CLR_VS_INPUT      RGB(0xFF, 0xFF, 0xFF)   /* input */
#define CLR_VS_STATUS     RGB(0x00, 0x7A, 0xCC)   /* statusBar */

/* Accents */
#define CLR_VS_ACCENT         RGB(0x00, 0x78, 0xD7)   /* selection */
#define CLR_VS_ACCENT_HOVER   RGB(0xEA, 0xF3, 0xFF)   /* hover */
#define CLR_VS_ACCENT_ACTIVE  RGB(0xD9, 0xEC, 0xFF)   /* pressed */
#define CLR_VS_SELECTION      RGB(0x00, 0x78, 0xD7)   

/* Borders */
#define CLR_VS_BORDER_PANEL   RGB(0xD4, 0xD5, 0xDA)   /* border */
#define CLR_VS_BORDER_ELEM    RGB(0xC5, 0xC5, 0xC5)   /* borderDark */
#define CLR_VS_BORDER_INPUT   RGB(0xCF, 0xCF, 0xCF)   /* input border */

/* Text */
#define CLR_VS_TEXT          RGB(0x22, 0x22, 0x22)   /* text */
#define CLR_VS_TEXT_MUTED    RGB(0x66, 0x66, 0x66)   /* textSecondary */
#define CLR_VS_TEXT_DISABLED RGB(0x99, 0x99, 0x99)   /* textDisabled */

/* Syntax-ish accents (used for badges/highlights) */
#define CLR_VS_KEYWORD   RGB(0x00, 0x00, 0xFF)   /* --vs-text-keyword */
#define CLR_VS_STRING    RGB(0xA3, 0x15, 0x15)   /* --vs-text-string  */
#define CLR_VS_COMMENT   RGB(0x00, 0x80, 0x00)   /* --vs-text-comment */

/* Buttons */
#define CLR_VS_BTN_BG         RGB(0xF5, 0xF5, 0xF5)
#define CLR_VS_BTN_BORDER     RGB(0xD0, 0xD0, 0xD0)
#define CLR_VS_BTN_HOVER_BG   RGB(0xFF, 0xFF, 0xFF)
#define CLR_VS_BTN_HOVER_BDR  RGB(0x99, 0xCF, 0xFF)
#define CLR_VS_BTN_PRESS_BG   RGB(0xD9, 0xEC, 0xFF)
#define CLR_VS_BTN_PRESS_BDR  RGB(0x00, 0x78, 0xD7)

/* Scrollbar */
#define CLR_VS_SCROLL_TRACK  RGB(0xF0, 0xF0, 0xF0)
#define CLR_VS_SCROLL_THUMB  RGB(0xC5, 0xC5, 0xC5)
#define CLR_VS_SCROLL_HOVER  RGB(0x99, 0x99, 0x99)

/* ──────────────────────────────────────────────────────────────────────────
 * Legacy semantic aliases. The rest of the GUI references these names; they are
 * remapped onto the VS2012 palette so the whole app reskins in one place.
 * ────────────────────────────────────────────────────────────────────────── */
#define CLR_BG_DARK        CLR_VS_MAIN       /* content/application surface */
#define CLR_SURFACE_DARK   CLR_VS_SIDEBAR    /* sidebar / panel surface    */
#define CLR_BORDER_DARK    CLR_VS_BORDER_PANEL
#define CLR_PRIMARY        CLR_VS_ACCENT
#define CLR_TEXT_PRIMARY   CLR_VS_TEXT
#define CLR_TEXT_SECONDARY CLR_VS_TEXT_MUTED
#define CLR_HOVER          CLR_VS_ACCENT_HOVER
#define CLR_BUTTON_BG      CLR_VS_BTN_BG

/* Size-grade ramp (kept for the per-archive/texture size indicators). */
#define CLR_SIZE_GREEN   RGB(0x16, 0xA3, 0x4A)
#define CLR_SIZE_LGREEN  RGB(0x4A, 0xDE, 0x80)
#define CLR_SIZE_YELLOW  RGB(0xFD, 0xE0, 0x47)
#define CLR_SIZE_DYELLOW RGB(0xEA, 0xB3, 0x08)
#define CLR_SIZE_ORANGE  RGB(0xFB, 0x92, 0x3C)
#define CLR_SIZE_DORANGE RGB(0xF9, 0x73, 0x16)
#define CLR_SIZE_RED     RGB(0xEF, 0x44, 0x44)
#define CLR_SIZE_DRED    RGB(0xDC, 0x26, 0x26)

void     theme_init(void);
void     theme_cleanup(void);

HFONT    theme_font_display(void);
HFONT    theme_font_title(void);
HFONT    theme_font_mono(void);
HFONT    theme_font_small(void);
HFONT    theme_font_small_bold(void);

HBRUSH   theme_brush_bg(void);
HBRUSH   theme_brush_surface(void);
HBRUSH   theme_brush_primary(void);

/* VS2012 is flat: draw a filled rectangle with an optional 1px square border.
 * (Replaces the old rounded-rect helper; radius is ignored/flat.) */
void     theme_fill_rect(HDC hdc, RECT *rc, COLORREF fill);
void     theme_flat_rect(HDC hdc, RECT *rc, COLORREF fill, COLORREF border);
void     theme_draw_rounded_rect(HDC hdc, RECT *rc, int radius, HBRUSH fill, HPEN border);

COLORREF theme_size_color(double mib);
COLORREF theme_archive_size_color(double mib);

#endif
