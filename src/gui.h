#ifndef EO_GUI_H
#define EO_GUI_H

#include <windows.h>
#include "types.h"

#define MAX_LOADED_YTDS 256

typedef struct {
    HWND hwnd_main;
    HWND hwnd_sidebar;
    HWND hwnd_content;
    HWND hwnd_status;
    HWND hwnd_search;
    HWND hwnd_header;

    YtdFile *ytds[MAX_LOADED_YTDS];
    int ytd_count;

    int scroll_y;
    int content_height;
    char search_filter[256];
    char status_text[512];

    /* Context menu selection */
    int sel_ytd_idx;
    int sel_tex_idx;
    
    bool use_gpu_encoding;
} AppState;

extern AppState g_app;

void gui_init(HINSTANCE hInst);
void gui_run(void);
void gui_add_ytd(const wchar_t *path);
void gui_render_content(HDC hdc, RECT *rc);
void gui_update_status(const char *fmt, ...);

/* gui_cards.c */
void gui_draw_ytd_card(HDC hdc, int x, int y, int w, YtdFile *ytd, bool hovered);
void gui_draw_texture_card(HDC hdc, int x, int y, int card_w, int card_h,
                           TextureEntry *tex, YtdFile *parent, bool hovered);

#endif
