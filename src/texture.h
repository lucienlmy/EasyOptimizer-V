#ifndef EO_TEXTURE_H
#define EO_TEXTURE_H

#include "types.h"

/* Decode compressed texture data to BGRA pixels for display */
uint8_t *tex_decode_to_bgra(const TextureEntry *tex, int mip_level, int *out_w, int *out_h);

/* Encode RGBA pixels to compressed format. Returns allocated data, sets out_size. */
uint8_t *tex_encode_bc(const uint8_t *rgba, int w, int h, TexFormat fmt, size_t *out_size);

/* Generate full mip chain from BGRA bitmap. Returns allocated data with all mips. */
uint8_t *tex_generate_mips(const uint8_t *rgba, int w, int h, TexFormat fmt,
                           int max_mips, int *out_mip_count, size_t *out_total_size);

#endif
