#ifndef EO_BC7ENC_WRAPPER_H
#define EO_BC7ENC_WRAPPER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void bc7enc_init(void);

/* Encode RGBA pixels to BC1-BC7 block compressed data.
   fmt: 0=BC1, 1=BC3, 2=BC4, 3=BC5, 4=BC7
   quality: 0.0-1.0 (maps to encoder quality levels)
   Returns malloc'd data, caller frees. Sets *out_size. */
uint8_t *bc7enc_compress(const uint8_t *rgba, int w, int h, int fmt,
                         float quality, size_t *out_size);

/* Decode a single BC7 block (16 bytes) to 16 RGBA pixels (64 bytes). */
int bc7enc_decompress_bc7_block(const uint8_t *block, uint8_t *rgba_out);

/* Decode a single BC1 block (8 bytes) to 16 RGBA pixels (64 bytes). */
void bc7enc_decompress_bc1_block(const uint8_t *block, uint8_t *rgba_out);

/* Decode a single BC3 block (16 bytes) to 16 RGBA pixels (64 bytes). */
void bc7enc_decompress_bc3_block(const uint8_t *block, uint8_t *rgba_out);

/* Decode a single BC4 block (8 bytes) to 16 RGBA pixels (64 bytes). */
void bc7enc_decompress_bc4_block(const uint8_t *block, uint8_t *rgba_out, int stride);

/* Decode a single BC5 block (16 bytes) to 16 RGBA pixels (64 bytes). */
void bc7enc_decompress_bc5_block(const uint8_t *block, uint8_t *rgba_out, int stride);

/* Resize RGBA pixels using stb_image_resize2.
   filter: 0=Default(Mitchell), 1=Box, 2=Triangle, 3=CubicBSpline, 4=CatmullRom, 5=Mitchell, 6=Point
   Returns malloc'd RGBA data. */
uint8_t *bc7enc_resize_rgba(const uint8_t *rgba, int src_w, int src_h,
                            int dst_w, int dst_h, int filter);

/* Load image from memory (PNG/JPG/BMP/TGA etc) to RGBA.
   Returns malloc'd RGBA data, sets *out_w, *out_h. Free with bc7enc_free(). */
uint8_t *bc7enc_load_image_mem(const uint8_t *data, size_t size,
                               int *out_w, int *out_h);

/* Load image from file path to RGBA. */
uint8_t *bc7enc_load_image_file(const wchar_t *path, int *out_w, int *out_h);

/* Free data allocated by bc7enc_* functions */
void bc7enc_free(void *ptr);

#ifdef __cplusplus
}
#endif

#endif
