#ifndef EO_TEXTURE_H
#define EO_TEXTURE_H

#include "types.h"

/* Decode compressed texture data to BGRA pixels for display */
uint8_t *tex_decode_to_bgra(const TextureEntry *tex, int mip_level, int *out_w, int *out_h);
bool tex_format_can_encode(TexFormat fmt);

/* Returns true if the decoded mip-0 BGRA has any pixel with alpha < threshold (250).
 * Used to decide whether BC3/BC7 can be safely downgraded to BC1 without losing info. */
bool tex_alpha_in_use(const TextureEntry *tex);

/* Original-state snapshot for instant Unload/revert.
 * tex_save_original captures the current bytes/dims once (no-op if already saved).
 * tex_revert_original restores them (no-op if nothing saved) and drops the snapshot.
 * tex_free_original releases the snapshot without reverting. */
void tex_save_original(TextureEntry *te);
bool tex_revert_original(TextureEntry *te);
void tex_free_original(TextureEntry *te);

/* Encode RGBA pixels to compressed format. Returns allocated data, sets out_size. */
uint8_t *tex_encode_bc(const uint8_t *rgba, int w, int h, TexFormat fmt, size_t *out_size);

/* Generate full mip chain from BGRA bitmap. Returns allocated data with all mips. */
uint8_t *tex_generate_mips(const uint8_t *rgba, int w, int h, TexFormat fmt,
                           int max_mips, int *out_mip_count, size_t *out_total_size);

#endif
