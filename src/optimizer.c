#include "optimizer.h"
#include "texture.h"
#include "bc7enc_wrapper.h"
#include "hash.h"
#include <stdlib.h>
#include <string.h>
#include <windows.h>

/* ── SHA256 via Windows CNG ────────────────────────────────────────── */

#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")

static void sha256_hex(const uint8_t *data, size_t len, char *out_hex) {
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    uint8_t digest[32];

    BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0);
    BCryptCreateHash(alg, &hash, NULL, 0, NULL, 0, 0);
    BCryptHashData(hash, (PUCHAR)data, (ULONG)len, 0);
    BCryptFinishHash(hash, digest, 32, 0);
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg, 0);

    for (int i = 0; i < 32; i++)
        sprintf(out_hex + i * 2, "%02x", digest[i]);
    out_hex[64] = 0;
}

/* ── Duplicate finder ──────────────────────────────────────────────── */

DupGroup *optimizer_find_duplicates(YtdFile **ytds, int ytd_count, int *out_group_count, DupCriterion criterion) {
    int total = 0;
    for (int y = 0; y < ytd_count; y++) total += ytds[y]->texture_count;
    if (total == 0) { *out_group_count = 0; return NULL; }

    typedef struct { char key[97]; int ytd_idx; int tex_idx; } Item;
    Item *items = (Item *)calloc(total, sizeof(Item));
    if (!items) { *out_group_count = 0; return NULL; }
    int item_count = 0;

    for (int y = 0; y < ytd_count; y++) {
        for (int t = 0; t < ytds[y]->texture_count; t++) {
            TextureEntry *te = &ytds[y]->textures[t];
            char name_lc[65];
            strncpy(name_lc, te->name, 64);
            name_lc[64] = 0;
            _strlwr_s(name_lc, 65);

            char hash_hex[65] = {0};
            bool have_hash = te->data && te->data_size > 0;
            if (have_hash && (criterion == DUP_BY_HASH || criterion == DUP_BY_NAME_AND_HASH)) {
                sha256_hex(te->data, te->data_size, hash_hex);
            }

            switch (criterion) {
                case DUP_BY_HASH:
                    if (!have_hash) continue;
                    strncpy(items[item_count].key, hash_hex, 96);
                    break;
                case DUP_BY_NAME_AND_HASH:
                    if (!have_hash) continue;
                    _snprintf(items[item_count].key, 97, "%s|%s", name_lc, hash_hex);
                    items[item_count].key[96] = 0;
                    break;
                case DUP_BY_NAME:
                default:
                    strncpy(items[item_count].key, name_lc, 96);
                    break;
            }
            items[item_count].ytd_idx = y;
            items[item_count].tex_idx = t;
            item_count++;
        }
    }

    int max_groups = item_count;
    DupGroup *groups = (DupGroup *)calloc(max_groups, sizeof(DupGroup));
    if (!groups) { free(items); *out_group_count = 0; return NULL; }
    int group_count = 0;

    for (int i = 0; i < item_count; i++) {
        bool found = false;
        for (int g = 0; g < group_count; g++) {
            if (strcmp(groups[g].hash_key, items[i].key) == 0) {
                int c = groups[g].count;
                DupEntry *grown = (DupEntry *)realloc(groups[g].entries, (c + 1) * sizeof(DupEntry));
                if (!grown) {
                    free(items);
                    optimizer_free_groups(groups, group_count);
                    *out_group_count = 0;
                    return NULL;
                }
                groups[g].entries = grown;
                groups[g].entries[c].ytd_index = items[i].ytd_idx;
                groups[g].entries[c].tex_index = items[i].tex_idx;
                groups[g].count++;
                found = true;
                break;
            }
        }
        if (!found) {
            strncpy(groups[group_count].hash_key, items[i].key, 96);
            groups[group_count].entries = (DupEntry *)malloc(sizeof(DupEntry));
            if (!groups[group_count].entries) {
                free(items);
                optimizer_free_groups(groups, group_count);
                *out_group_count = 0;
                return NULL;
            }
            groups[group_count].entries[0].ytd_index = items[i].ytd_idx;
            groups[group_count].entries[0].tex_index = items[i].tex_idx;
            groups[group_count].count = 1;
            group_count++;
        }
    }

    free(items);

    int dup_count = 0;
    for (int g = 0; g < group_count; g++) {
        if (groups[g].count > 1) {
            if (dup_count != g) groups[dup_count] = groups[g];
            dup_count++;
        } else {
            free(groups[g].entries);
        }
    }
    *out_group_count = dup_count;
    return groups;
}

void optimizer_free_groups(DupGroup *groups, int count) {
    if (!groups) return;
    for (int i = 0; i < count; i++) free(groups[i].entries);
    free(groups);
}

int optimizer_smart_resize(YtdFile *ytd, int max_width, int max_height, TexFormat target_fmt, int max_mips) {
    int resized = 0;
    bc7enc_init();

    for (int i = 0; i < ytd->texture_count; i++) {
        TextureEntry *te = &ytd->textures[i];
        if (te->width <= max_width && te->height <= max_height)
            continue;

        /* Decode to BGRA */
        int dec_w, dec_h;
        uint8_t *bgra = tex_decode_to_bgra(te, 0, &dec_w, &dec_h);
        if (!bgra) continue;

        /* Convert BGRA → RGBA for encoder */
        size_t px_count = (size_t)dec_w * dec_h;
        uint8_t *rgba = (uint8_t *)malloc(px_count * 4);
        if (!rgba) { free(bgra); continue; }
        for (size_t p = 0; p < px_count; p++) {
            rgba[p*4+0] = bgra[p*4+2];
            rgba[p*4+1] = bgra[p*4+1];
            rgba[p*4+2] = bgra[p*4+0];
            rgba[p*4+3] = bgra[p*4+3];
        }
        free(bgra);

        /* Calculate new dimensions (halve until within limits) */
        int new_w = dec_w, new_h = dec_h;
        while (new_w > max_width || new_h > max_height) {
            new_w = new_w > 1 ? new_w / 2 : 1;
            new_h = new_h > 1 ? new_h / 2 : 1;
        }
        if (new_w < 4) new_w = 4;
        if (new_h < 4) new_h = 4;

        /* Resize using stb_image_resize2 (Mitchell filter) */
        uint8_t *resized_rgba = bc7enc_resize_rgba(rgba, dec_w, dec_h, new_w, new_h, 5);
        free(rgba);
        if (!resized_rgba) continue;

        /* Re-encode with mips */
        TexFormat enc_fmt = target_fmt == TEX_FMT_UNKNOWN ? te->format : target_fmt;
        if (!tex_format_can_encode(enc_fmt)) {
            bc7enc_free(resized_rgba);
            continue;
        }
        int mip_count = 0;
        size_t total_size = 0;
        uint8_t *new_data = tex_generate_mips(resized_rgba, new_w, new_h, enc_fmt,
                                              (max_mips == -2 ? te->mip_count : (max_mips == -1 ? 13 : max_mips)), &mip_count, &total_size);
        bc7enc_free(resized_rgba);
        if (!new_data) continue;

        /* Snapshot the pre-edit state once so Unload can revert instantly. */
        tex_save_original(te);

        /* Replace texture data */
        free(te->data);
        te->data = new_data;
        te->data_size = total_size;
        te->width = new_w;
        te->height = new_h;
        te->format = enc_fmt;
        te->mip_count = mip_count;
        resized++;
    }

    if (resized > 0)
        ytd->modified = true;

    return resized;
}

/* ── Migrate duplicates ────────────────────────────────────────────── */

static size_t archive_total_data_size(const YtdFile *a) {
    size_t total = 0;
    for (int i = 0; i < a->texture_count; i++)
        total += a->textures[i].data_size;
    return total;
}

static YtdFile *make_consolidated_ytd(const wchar_t *base_dir, int seq) {
    YtdFile *out = (YtdFile *)calloc(1, sizeof(YtdFile));
    if (!out) return NULL;
    out->type = ARCHIVE_YTD;
    out->expanded = true;
    out->modified = false;   /* preview: not yet committed */
    out->is_preview = true;
    _snprintf(out->name, EO_MAX_NAME, "consolidated_textures_%d.ytd", seq);
    out->name[EO_MAX_NAME - 1] = 0;
    wchar_t wname[EO_MAX_NAME];
    MultiByteToWideChar(CP_UTF8, 0, out->name, -1, wname, EO_MAX_NAME);
    if (base_dir && base_dir[0]) {
        _snwprintf(out->file_path, EO_MAX_PATH, L"%s\\%s", base_dir, wname);
    } else {
        wcsncpy(out->file_path, wname, EO_MAX_PATH - 1);
    }
    out->file_path[EO_MAX_PATH - 1] = 0;
    out->textures = NULL;
    out->texture_count = 0;
    return out;
}

static bool consolidated_append(YtdFile *dst, const TextureEntry *src) {
    TextureEntry *grow = (TextureEntry *)realloc(dst->textures,
        (dst->texture_count + 1) * sizeof(TextureEntry));
    if (!grow) return false;
    dst->textures = grow;
    TextureEntry *nt = &dst->textures[dst->texture_count];
    memcpy(nt, src, sizeof(TextureEntry));
    nt->wtd_meta = NULL;
    /* The copy must not share the source's snapshot pointer (double-free). */
    nt->has_orig = false;
    nt->orig_data = NULL;
    nt->orig_data_size = 0;
    if (src->data && src->data_size > 0) {
        nt->data = (uint8_t *)malloc(src->data_size);
        if (!nt->data) return false;
        memcpy(nt->data, src->data, src->data_size);
    } else {
        nt->data = NULL;
        nt->data_size = 0;
    }
    dst->texture_count++;
    return true;
}

static void archive_remove_texture(YtdFile *a, int idx) {
    if (idx < 0 || idx >= a->texture_count) return;
    free(a->textures[idx].data);
    free(a->textures[idx].orig_data);
    free(a->textures[idx].wtd_meta);
    for (int i = idx; i < a->texture_count - 1; i++)
        a->textures[i] = a->textures[i + 1];
    a->texture_count--;
    a->modified = true;
}

int optimizer_build_consolidation(YtdFile **io_ytds, int *io_ytd_count, int max_ytds,
                                  DupCriterion criterion, MigrateStrategy strategy,
                                  PendingRemoval **out_removals, int *out_removal_count,
                                  int *out_dup_groups, int *out_textures, int *out_consolidated) {
    if (out_removals) *out_removals = NULL;
    if (out_removal_count) *out_removal_count = 0;
    if (out_dup_groups) *out_dup_groups = 0;
    if (out_textures) *out_textures = 0;
    if (out_consolidated) *out_consolidated = 0;
    if (!io_ytds || !io_ytd_count || *io_ytd_count == 0) return 0;

    int group_count = 0;
    DupGroup *groups = optimizer_find_duplicates(io_ytds, *io_ytd_count, &group_count, criterion);
    if (out_dup_groups) *out_dup_groups = group_count;
    if (group_count == 0) { optimizer_free_groups(groups, group_count); return 0; }

    /* Removal list (by pointer, robust to later re-sorting). */
    int rem_cap = 64, rem_n = 0;
    PendingRemoval *rem = (PendingRemoval *)malloc(rem_cap * sizeof(PendingRemoval));

    /* Base dir for consolidated YTDs: dir of first existing YTD. */
    wchar_t base_dir[EO_MAX_PATH] = {0};
    for (int i = 0; i < *io_ytd_count; i++) {
        if (io_ytds[i]->file_path[0]) {
            wcsncpy(base_dir, io_ytds[i]->file_path, EO_MAX_PATH - 1);
            base_dir[EO_MAX_PATH - 1] = 0;
            wchar_t *slash = wcsrchr(base_dir, L'\\');
            if (!slash) slash = wcsrchr(base_dir, L'/');
            if (slash) *slash = 0;
            else base_dir[0] = 0;
            break;
        }
    }

    YtdFile *cur_consol = NULL;
    int consol_seq = 1;
    int consol_created = 0;
    int moved = 0;

    if (!rem) { optimizer_free_groups(groups, group_count); return 0; }

    #define PUSH_REMOVAL(ytd_, tex_) do { \
        if (rem_n == rem_cap) { \
            PendingRemoval *grow_ = (PendingRemoval *)realloc(rem, (size_t)rem_cap * 2 * sizeof(PendingRemoval)); \
            if (!grow_) goto done_groups; \
            rem = grow_; rem_cap *= 2; \
        } \
        rem[rem_n].ytd = (ytd_); rem[rem_n].tex_index = (tex_); rem[rem_n].consolidated = cur_consol; rem_n++; moved++; \
    } while (0)

    for (int g = 0; g < group_count; g++) {
        DupGroup *dg = &groups[g];
        if (dg->count < 2) continue;

        DupEntry *master = &dg->entries[0];
        TextureEntry *master_tex = &io_ytds[master->ytd_index]->textures[master->tex_index];

        if (strategy == MIGRATE_MIXED) {
            /* Master stays in its original; one copy goes to consolidated; dups dropped. */
            if (!cur_consol || archive_total_data_size(cur_consol) + master_tex->data_size > MIGRATE_GREEN_LIMIT) {
                if (*io_ytd_count >= max_ytds) break;
                cur_consol = make_consolidated_ytd(base_dir, consol_seq++);
                if (!cur_consol) break;
                io_ytds[(*io_ytd_count)++] = cur_consol;
                consol_created++;
            }
            consolidated_append(cur_consol, master_tex);
            for (int e = 1; e < dg->count; e++)
                PUSH_REMOVAL(io_ytds[dg->entries[e].ytd_index], dg->entries[e].tex_index);
        } else { /* MIGRATE_REMOVE_DUPS: every distinct name → consolidated; all originals dropped. */
            for (int e = 0; e < dg->count; e++) {
                DupEntry *ent = &dg->entries[e];
                TextureEntry *src = &io_ytds[ent->ytd_index]->textures[ent->tex_index];
                bool name_seen = false;
                for (int p = 0; p < e; p++) {
                    TextureEntry *prev = &io_ytds[dg->entries[p].ytd_index]->textures[dg->entries[p].tex_index];
                    if (_stricmp(prev->name, src->name) == 0) { name_seen = true; break; }
                }
                if (!name_seen) {
                    if (!cur_consol || archive_total_data_size(cur_consol) + src->data_size > MIGRATE_GREEN_LIMIT) {
                        if (*io_ytd_count >= max_ytds) goto done_groups;
                        cur_consol = make_consolidated_ytd(base_dir, consol_seq++);
                        if (!cur_consol) goto done_groups;
                        io_ytds[(*io_ytd_count)++] = cur_consol;
                        consol_created++;
                    }
                    consolidated_append(cur_consol, src);
                }
                PUSH_REMOVAL(io_ytds[ent->ytd_index], ent->tex_index);
            }
        }
    }
done_groups:
    #undef PUSH_REMOVAL

    optimizer_free_groups(groups, group_count);

    if (out_removals) *out_removals = rem;
    else free(rem);
    if (out_removal_count) *out_removal_count = rem_n;
    if (out_textures) *out_textures = moved;
    if (out_consolidated) *out_consolidated = consol_created;
    return consol_created;
}

void optimizer_apply_removals(PendingRemoval *removals, int count) {
    if (!removals || count <= 0) return;

    /* Sort by (ytd pointer, tex_index descending) so removals within one YTD
     * are applied high-index-first and stay valid. */
    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            bool swap = false;
            if (removals[j].ytd > removals[i].ytd) swap = true;
            else if (removals[j].ytd == removals[i].ytd && removals[j].tex_index > removals[i].tex_index) swap = true;
            if (swap) { PendingRemoval tmp = removals[i]; removals[i] = removals[j]; removals[j] = tmp; }
        }
    }
    for (int i = 0; i < count; i++) {
        /* Skip removals whose consolidated was flagged "Maintain originals". */
        if (removals[i].consolidated && removals[i].consolidated->keep_originals) continue;
        archive_remove_texture(removals[i].ytd, removals[i].tex_index);
    }
}
