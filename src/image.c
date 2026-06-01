#include "image.h"
#include <string.h>

HBITMAP image_create_bitmap(const uint8_t *bgra, int w, int h) {
    if (!bgra || w <= 0 || h <= 0) return NULL;

    BITMAPINFO info;
    memset(&info, 0, sizeof(info));
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = w;
    info.bmiHeader.biHeight = -h;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void *bits = NULL;
    HBITMAP bitmap = CreateDIBSection(NULL, &info, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!bitmap || !bits) {
        if (bitmap) DeleteObject(bitmap);
        return NULL;
    }

    memcpy(bits, bgra, (size_t)w * h * 4);
    return bitmap;
}
