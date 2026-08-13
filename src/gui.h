#ifndef EO_GUI_H
#define EO_GUI_H

#include <windows.h>
#include "types.h"

/* Upper bound on simultaneously-loaded archives. Raised well above the old 256
 * so large RPF mod packs (NVE entries number in the thousands) fit. The array
 * is just pointers (8 bytes each = 64 KB); real memory scales only with what
 * actually loads, and OOM is handled gracefully (allocations are NULL-checked). */
#define MAX_LOADED_YTDS 8192
#define RPF_ENTRY_H 42

typedef struct {
    HWND hwnd_main;
    HWND hwnd_sidebar;
    HWND hwnd_content;
    HWND hwnd_status;
    HWND hwnd_totals;
    HWND hwnd_search;
    HWND hwnd_menubar;

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
    RECT rc_sponsor;
    bool sponsor_hovered;

    int selected_row_idx;
    int hovered_row_idx;
} AppState;

extern AppState g_app;

void gui_init(HINSTANCE hInst);
void gui_run(void);
void gui_add_ytd(const wchar_t *path);
void gui_render_content(HDC hdc, RECT *rc);
void gui_update_status(const char *fmt, ...);

/* gui_cards.c */
void gui_draw_ytd_card(HDC hdc, int x, int y, int w, YtdFile *ytd,
                       YtdFile **all_archives, int archive_count, bool hovered);
void gui_draw_rpf_entry_row(HDC hdc, int x, int y, int w, YtdFile *ytd);

/* Card action buttons: "Save" rewrites the archive in its own game format,
 * "Export" dumps its textures as loose .dds. Rects are card-relative; both the
 * painter and the click handler use these so the two can never drift apart.
 * Returns false when this card shows no action buttons. */
#define CARD_BTN_W   62
#define CARD_BTN_GAP 6
bool gui_card_action_rects(const YtdFile *ytd, int w, RECT *out_save, RECT *out_export);
bool gui_rpf_row_action_rects(const YtdFile *ytd, int w, RECT *out_save, RECT *out_export);
void gui_draw_texture_card(HDC hdc, int x, int y, int card_w, int card_h,
                           TextureEntry *tex, YtdFile *parent, bool hovered);

#endif
