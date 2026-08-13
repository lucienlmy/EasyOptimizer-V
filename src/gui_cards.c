#include "gui.h"
#include "theme.h"
#include "texture.h"
#include "image.h"
#include <stdio.h>

static size_t gui_archive_total_size(const YtdFile *archive) {
    size_t total = 0;
    if (!archive || !archive->textures) return 0;
    for (int i = 0; i < archive->texture_count; i++)
        total += archive->textures[i].data_size;
    return total;
}

static size_t gui_rpf_total_size(const YtdFile *group, YtdFile **all_archives, int archive_count) {
    size_t total = 0;
    if (!group || !all_archives) return 0;
    for (int i = 0; i < archive_count; i++) {
        YtdFile *child = all_archives[i];
        if (child && child->rpf_parent == group)
            total += gui_archive_total_size(child);
    }
    return total;
}

/* ── YTD Folder Card ───────────────────────────────────────────────── */

void gui_draw_ytd_card(HDC hdc, int x, int y, int w, YtdFile *ytd,
                       YtdFile **all_archives, int archive_count, bool hovered) {
    RECT rc = {x, y, x + w, y + 56};

    /* VS2012 flat card: solid fill with 1px square border. */
    COLORREF fill_clr = hovered ? CLR_VS_ACCENT_HOVER : CLR_VS_SIDEBAR;
    COLORREF border_clr = ytd->is_preview ? RGB(230, 160, 30)
                                          : (ytd->expanded ? CLR_VS_ACCENT : CLR_VS_BORDER_PANEL);
    theme_flat_rect(hdc, &rc, fill_clr, border_clr);

    /* Folder icon (simple colored rectangle) */
    COLORREF icon_clr = CLR_PRIMARY;
    HBRUSH icon_brush = CreateSolidBrush(icon_clr);
    RECT icon_rc = {x + 16, y + 18, x + 48, y + 44};
    FillRect(hdc, &icon_rc, icon_brush);
    RECT tab_rc = {x + 16, y + 12, x + 32, y + 20};
    FillRect(hdc, &tab_rc, icon_brush);
    DeleteObject(icon_brush);

    /* Name */
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, CLR_TEXT_PRIMARY);
    SelectObject(hdc, theme_font_title());

    wchar_t wname[256];
    MultiByteToWideChar(CP_UTF8, 0, ytd->name, -1, wname, 256);
    RECT name_rc = {x + 60, y + 10, x + w - 40, y + 30};
    DrawTextW(hdc, wname, -1, &name_rc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

    /* Info line */
    size_t total_size = ytd->is_rpf_group
        ? gui_rpf_total_size(ytd, all_archives, archive_count)
        : gui_archive_total_size(ytd);
    double total_mib = total_size / (1024.0 * 1024.0);

    wchar_t info[128];
    if (ytd->is_rpf_group)
        _snwprintf(info, 128, L"RPF archive | %d files | %.2f MiB total | expand to retrieve list",
            ytd->rpf_child_count, total_mib);
    else if (ytd->is_preview)
        _snwprintf(info, 128, L"PREVIEW | %d textures | %.2f MiB", ytd->texture_count, total_mib);
    else
        _snwprintf(info, 128, L"%d textures | %.2f MiB", ytd->texture_count, total_mib);

    SetTextColor(hdc, theme_archive_size_color(total_mib));
    SelectObject(hdc, theme_font_small());
    RECT info_rc = {x + 60, y + 30, x + w - 190, y + 48};   /* clear of the action buttons */
    DrawTextW(hdc, info, -1, &info_rc, DT_LEFT | DT_SINGLELINE);

    /* Preview consolidated YTDs get a "Maintain" toggle to keep originals. */
    if (ytd->is_preview) {
        RECT btn = {x + w - 130, y + 16, x + w - 44, y + 40};
        bool on = ytd->keep_originals;
        COLORREF bf = on ? RGB(0x16, 0xA3, 0x4A) : CLR_VS_BTN_BG;
        COLORREF bb = on ? RGB(0x4A, 0xDE, 0x80) : CLR_VS_BORDER_ELEM;
        theme_flat_rect(hdc, &btn, bf, bb);
        SetTextColor(hdc, on ? RGB(255, 255, 255) : CLR_VS_TEXT);
        SelectObject(hdc, theme_font_small_bold());
        DrawTextW(hdc, on ? L"Keeping" : L"Maintain", -1, &btn,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    } else if (ytd->from_rpf) {
        RECT btn = {x + w - 116, y + 16, x + w - 44, y + 40};
        theme_flat_rect(hdc, &btn, CLR_VS_BTN_BG, CLR_VS_BTN_BORDER);
        SetTextColor(hdc, RGB(0x8A, 0x1F, 0x1F));
        SelectObject(hdc, theme_font_small_bold());
        DrawTextW(hdc, L"Unload", -1, &btn, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    /* Save = rewrite the file in its own game format; Export = dump textures as
     * loose .dds. Geometry comes from the shared helper so the hit-tester in
     * gui.c cannot drift out of sync with what is painted here. */
    RECT save_b, exp_b;
    if (gui_card_action_rects(ytd, w, &save_b, &exp_b)) {
        OffsetRect(&save_b, x, y);
        OffsetRect(&exp_b, x, y);

        theme_flat_rect(hdc, &save_b, CLR_VS_BTN_BG, CLR_VS_BTN_BORDER);
        SetTextColor(hdc, RGB(0x1F, 0x4E, 0x8A));
        SelectObject(hdc, theme_font_small_bold());
        DrawTextW(hdc, L"Save", -1, &save_b, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        theme_flat_rect(hdc, &exp_b, CLR_VS_BTN_BG, CLR_VS_BTN_BORDER);
        SetTextColor(hdc, RGB(0x1F, 0x6B, 0x3A));
        DrawTextW(hdc, L"Export", -1, &exp_b, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    /* Expand arrow */
    SetTextColor(hdc, CLR_TEXT_PRIMARY);
    SelectObject(hdc, theme_font_title());
    const wchar_t *arrow = ytd->expanded ? L"\x25BC" : L"\x25B6";
    RECT arrow_rc = {x + w - 30, y + 16, x + w - 8, y + 40};
    DrawTextW(hdc, arrow, -1, &arrow_rc, DT_CENTER | DT_SINGLELINE);
}

/* Card action buttons, in card-relative coordinates. Painter and hit-tester both
 * call this, so the clickable area always matches the drawn one. */
bool gui_card_action_rects(const YtdFile *ytd, int w, RECT *out_save, RECT *out_export) {
    if (!ytd || ytd->is_preview || ytd->is_rpf_group || ytd->texture_count <= 0)
        return false;
    int right = ytd->from_rpf ? (w - 124) : (w - 44);   /* left of Unload / arrow */
    SetRect(out_save, right - CARD_BTN_W, 16, right, 40);
    SetRect(out_export, right - CARD_BTN_W * 2 - CARD_BTN_GAP, 16,
                        right - CARD_BTN_W - CARD_BTN_GAP, 40);
    return true;
}

/* Same, for a row inside an expanded RPF group. */
bool gui_rpf_row_action_rects(const YtdFile *ytd, int w, RECT *out_save, RECT *out_export) {
    if (!ytd || ytd->texture_count <= 0) return false;
    int right = w - 116;                                /* left of Unload */
    SetRect(out_save, right - CARD_BTN_W, 9, right, 33);
    SetRect(out_export, right - CARD_BTN_W * 2 - CARD_BTN_GAP, 9,
                        right - CARD_BTN_W - CARD_BTN_GAP, 33);
    return true;
}

void gui_draw_rpf_entry_row(HDC hdc, int x, int y, int w, YtdFile *ytd) {
    RECT rc = {x, y, x + w, y + RPF_ENTRY_H};
    /* VS2012 flat row: dark surface + panel border */
    theme_flat_rect(hdc, &rc, CLR_VS_MAIN, CLR_VS_BORDER_PANEL);

    wchar_t name[EO_MAX_NAME];
    MultiByteToWideChar(CP_UTF8, 0, ytd->name, -1, name, EO_MAX_NAME);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, CLR_TEXT_PRIMARY);
    SelectObject(hdc, theme_font_small_bold());
    RECT name_rc = {x + 14, y + 6, x + w - 256, y + 22};   /* clear of Save/Export/Unload */
    DrawTextW(hdc, name, -1, &name_rc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

    size_t total_size = gui_archive_total_size(ytd);
    double total_mib = total_size / (1024.0 * 1024.0);
    wchar_t info[128];
    if (!ytd->textures)
        _snwprintf(info, 128, L"listed only | preview unavailable");
    else
        _snwprintf(info, 128, L"%d textures | %.2f MiB | read-only from RPF",
            ytd->texture_count, total_mib);
    SetTextColor(hdc, CLR_TEXT_SECONDARY);
    SelectObject(hdc, theme_font_small());
    RECT info_rc = {x + 14, y + 22, x + w - 256, y + 38};
    DrawTextW(hdc, info, -1, &info_rc, DT_LEFT | DT_SINGLELINE);

    RECT unload = {x + w - 108, y + 9, x + w - 48, y + 33};
    theme_flat_rect(hdc, &unload, CLR_VS_BTN_BG, CLR_VS_BTN_BORDER);
    SetTextColor(hdc, RGB(0x8A, 0x1F, 0x1F));
    SelectObject(hdc, theme_font_small_bold());
    DrawTextW(hdc, L"Unload", -1, &unload, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    /* Save / Export, left of Unload — same split as the file card. */
    RECT row_save, row_exp;
    if (gui_rpf_row_action_rects(ytd, w, &row_save, &row_exp)) {
        OffsetRect(&row_save, x, y);
        OffsetRect(&row_exp, x, y);

        theme_flat_rect(hdc, &row_save, CLR_VS_BTN_BG, CLR_VS_BTN_BORDER);
        SetTextColor(hdc, RGB(0x1F, 0x4E, 0x8A));
        SelectObject(hdc, theme_font_small_bold());
        DrawTextW(hdc, L"Save", -1, &row_save, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        theme_flat_rect(hdc, &row_exp, CLR_VS_BTN_BG, CLR_VS_BTN_BORDER);
        SetTextColor(hdc, RGB(0x1F, 0x6B, 0x3A));
        DrawTextW(hdc, L"Export", -1, &row_exp, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    SetTextColor(hdc, CLR_TEXT_PRIMARY);
    SelectObject(hdc, theme_font_title());
    RECT arrow = {x + w - 36, y + 10, x + w - 8, y + 34};
    DrawTextW(hdc, ytd->expanded ? L"\x25BC" : L"\x25B6", -1, &arrow,
              DT_CENTER | DT_SINGLELINE);
}

/* ── Texture Card ──────────────────────────────────────────────────── */

void gui_draw_texture_card(HDC hdc, int x, int y, int card_w, int card_h,
                           TextureEntry *tex, YtdFile *parent, bool hovered) {
    RECT rc = {x, y, x + card_w, y + card_h};

    /* VS2012 flat card: square, 1px border, accent on hover */
    COLORREF bdr = hovered ? CLR_VS_ACCENT : CLR_VS_BORDER_PANEL;
    theme_flat_rect(hdc, &rc, CLR_VS_SIDEBAR, bdr);

    int footer_h = 80;
    int img_h = card_h - footer_h;

    /* Image area (VS2012 editor-dark background) */
    RECT img_rc = {x + 1, y + 1, x + card_w - 1, y + img_h};
    theme_fill_rect(hdc, &img_rc, CLR_VS_EDITOR);

    /* Draw texture preview from the cached thumbnail, decoding only when the
     * cache is cold or an edit marked it stale. The decode picks the smallest
     * mip that still covers the card, so a 2048² BC7 costs a 256² block-decode
     * instead of a full one — and only once, not once per frame. */
    int avail_w = card_w - 2;
    int avail_h = img_h - 2;
    if (tex->preview_dirty) tex_free_preview(tex);

    if (!tex->preview_bmp) {
        int target = avail_w > avail_h ? avail_w : avail_h;
        int tw = 0, th = 0;
        int level = tex_preview_mip(tex, target);
        uint8_t *pixels = tex_decode_to_bgra(tex, level, &tw, &th);
        /* Some dictionaries advertise more mips than they actually store, which
         * makes the deeper level fail its bounds check. Fall back to mip 0 so a
         * bad mip_count costs speed, not a blank card. */
        if (!pixels && level > 0)
            pixels = tex_decode_to_bgra(tex, 0, &tw, &th);
        if (pixels) {
            if (tw > 0 && th > 0) {
                tex->preview_bmp = (void *)image_create_bitmap(pixels, tw, th);
                tex->preview_w = tw;
                tex->preview_h = th;
            }
            free(pixels);
        }
        tex->preview_dirty = false;
    }

    if (tex->preview_bmp && tex->preview_w > 0 && tex->preview_h > 0) {
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP oldBM = (HBITMAP)SelectObject(memDC, (HBITMAP)tex->preview_bmp);

        float ratio_w = (float)avail_w / tex->preview_w;
        float ratio_h = (float)avail_h / tex->preview_h;
        float ratio = ratio_w < ratio_h ? ratio_w : ratio_h;
        int dw = (int)(tex->preview_w * ratio);
        int dh = (int)(tex->preview_h * ratio);
        int dx = x + 1 + (avail_w - dw) / 2;
        int dy = y + 1 + (avail_h - dh) / 2;

        SetStretchBltMode(hdc, HALFTONE);
        StretchBlt(hdc, dx, dy, dw, dh, memDC, 0, 0, tex->preview_w, tex->preview_h, SRCCOPY);

        SelectObject(memDC, oldBM);
        DeleteDC(memDC);
    }

    /* Format badge */
    SetBkMode(hdc, TRANSPARENT);
    const char *fmt_str = tex_format_name(tex->format);
    char short_fmt[64];
    if (strncmp(fmt_str, "D3DFMT_", 7) == 0)
        strncpy(short_fmt, fmt_str + 7, 64);
    else
        strncpy(short_fmt, fmt_str, 64);

    wchar_t wfmt[64];
    MultiByteToWideChar(CP_UTF8, 0, short_fmt, -1, wfmt, 64);

    SelectObject(hdc, theme_font_mono());
    SIZE fmtSize;
    GetTextExtentPoint32W(hdc, wfmt, (int)wcslen(wfmt), &fmtSize);

    RECT badge_rc = {x + card_w - fmtSize.cx - 16, y + 6,
                     x + card_w - 6, y + 6 + fmtSize.cy + 4};
    /* VS2012 accent badge: dark menu bg + accent border for a polished look. */
    theme_flat_rect(hdc, &badge_rc, CLR_VS_MENU, CLR_VS_BORDER_PANEL);
    SetTextColor(hdc, CLR_VS_KEYWORD);
    DrawTextW(hdc, wfmt, -1, &badge_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    /* Footer: name, size, dimensions, mips */
    int text_y = y + img_h + 10;
    int text_x = x + 12;

    /* Name */
    wchar_t wname[256];
    MultiByteToWideChar(CP_UTF8, 0, tex->name, -1, wname, 256);
    SetTextColor(hdc, CLR_TEXT_PRIMARY);
    SelectObject(hdc, theme_font_title());
    RECT name_rc = {text_x, text_y, x + card_w - 80, text_y + 20};
    DrawTextW(hdc, wname, -1, &name_rc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

    /* Size */
    double mib = tex->data_size / (1024.0 * 1024.0);
    wchar_t size_str[32];
    _snwprintf(size_str, 32, L"%.2f MiB", mib);
    SetTextColor(hdc, theme_size_color(mib));
    SelectObject(hdc, theme_font_small_bold());
    RECT size_rc = {x + card_w - 80, text_y, x + card_w - 10, text_y + 20};
    DrawTextW(hdc, size_str, -1, &size_rc, DT_RIGHT | DT_SINGLELINE);

    /* Meta info */
    text_y += 22;
    SetTextColor(hdc, CLR_TEXT_SECONDARY);
    SelectObject(hdc, theme_font_small());

    wchar_t meta[128];
    _snwprintf(meta, 128, L"%d x %d", tex->width, tex->height);
    RECT meta_rc = {text_x, text_y, x + card_w - 12, text_y + 14};
    DrawTextW(hdc, meta, -1, &meta_rc, DT_LEFT | DT_SINGLELINE);

    text_y += 14;
    wchar_t mips_str[32];
    _snwprintf(mips_str, 32, L"Mips: %d", tex->mip_count);
    RECT mips_rc = {text_x, text_y, x + card_w - 12, text_y + 14};
    DrawTextW(hdc, mips_str, -1, &mips_rc, DT_LEFT | DT_SINGLELINE);

    /* Parent YTD name */
    text_y += 14;
    wchar_t wparent[256];
    MultiByteToWideChar(CP_UTF8, 0, parent->name, -1, wparent, 256);
    RECT parent_rc = {text_x, text_y, x + card_w - 12, text_y + 14};
    DrawTextW(hdc, wparent, -1, &parent_rc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

    /* VS2012 flat Edit button */
    RECT edit_rc = {x + card_w - 62, y + card_h - 32, x + card_w - 12, y + card_h - 12};
    theme_flat_rect(hdc, &edit_rc, CLR_VS_BTN_BG, CLR_VS_BORDER_ELEM);
    SetTextColor(hdc, CLR_VS_TEXT);
    SelectObject(hdc, theme_font_small());
    DrawTextW(hdc, L"Edit", -1, &edit_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}
