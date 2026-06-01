#ifndef EO_IMAGE_H
#define EO_IMAGE_H

#include <stdint.h>
#include <windows.h>

/* Create a Win32 HBITMAP from BGRA pixel data for display */
HBITMAP image_create_bitmap(const uint8_t *bgra, int w, int h);

/* Resize RGBA image using bilinear interpolation */
uint8_t *image_resize(const uint8_t *rgba, int src_w, int src_h, int dst_w, int dst_h);

/* Load PNG/BMP from file to RGBA (using GDI+) */
uint8_t *image_load_file(const wchar_t *path, int *out_w, int *out_h);

#endif
