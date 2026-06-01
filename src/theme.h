#ifndef EO_THEME_H
#define EO_THEME_H

#include <windows.h>

#define CLR_BG_DARK      RGB(0x18, 0x18, 0x18)
#define CLR_SURFACE_DARK RGB(0x20, 0x20, 0x20)
#define CLR_BORDER_DARK  RGB(0x33, 0x33, 0x33)
#define CLR_PRIMARY      RGB(0x00, 0x78, 0xD4)
#define CLR_TEXT_PRIMARY  RGB(0xFF, 0xFF, 0xFF)
#define CLR_TEXT_SECONDARY RGB(0xA1, 0xA1, 0xA1)
#define CLR_HOVER        RGB(0x28, 0x28, 0x28)
#define CLR_BUTTON_BG    RGB(0x3C, 0x3C, 0x3C)

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

void     theme_draw_rounded_rect(HDC hdc, RECT *rc, int radius, HBRUSH fill, HPEN border);
COLORREF theme_size_color(double mib);
/* Color for a whole archive's size: green up to the ~16 MiB streaming limit,
 * then escalating to red as it exceeds it. */
COLORREF theme_archive_size_color(double mib);

#endif
