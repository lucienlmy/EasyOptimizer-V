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

DupGroup *optimizer_find_duplicates(YtdFile **ytds, int ytd_count, int *out_group_count, bool by_hash) {
    int total = 0;
    for (int y = 0; y < ytd_count; y++) total += ytds[y]->texture_count;
    if (total == 0) { *out_group_count = 0; return NULL; }

    typedef struct { char key[65]; int ytd_idx; int tex_idx; } Item;
    Item *items = (Item *)calloc(total, sizeof(Item));
    int item_count = 0;

    for (int y = 0; y < ytd_count; y++) {
        for (int t = 0; t < ytds[y]->texture_count; t++) {
            TextureEntry *te = &ytds[y]->textures[t];
            if (by_hash && te->data && te->data_size > 0) {
                sha256_hex(te->data, te->data_size, items[item_count].key);
            } else {
                strncpy(items[item_count].key, te->name, 64);
                _strlwr_s(items[item_count].key, 65);
            }
            items[item_count].ytd_idx = y;
            items[item_count].tex_idx = t;
            item_count++;
        }
    }

    int max_groups = item_count;
    DupGroup *groups = (DupGroup *)calloc(max_groups, sizeof(DupGroup));
    int group_count = 0;

    for (int i = 0; i < item_count; i++) {
        bool found = false;
        for (int g = 0; g < group_count; g++) {
            if (strcmp(groups[g].hash_key, items[i].key) == 0) {
                int c = groups[g].count;
                groups[g].entries = (DupEntry *)realloc(groups[g].entries, (c + 1) * sizeof(DupEntry));
                groups[g].entries[c].ytd_index = items[i].ytd_idx;
                groups[g].entries[c].tex_index = items[i].tex_idx;
                groups[g].count++;
                found = true;
                break;
            }
        }
        if (!found) {
            strncpy(groups[group_count].hash_key, items[i].key, 64);
            groups[group_count].entries = (DupEntry *)malloc(sizeof(DupEntry));
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
