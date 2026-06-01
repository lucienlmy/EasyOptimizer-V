#include "dds.h"
#include <string.h>
#include <stdlib.h>

#define DDS_MAGIC       0x20534444
#define DDSD_CAPS       0x1
#define DDSD_HEIGHT     0x2
#define DDSD_WIDTH      0x4
#define DDSD_PITCH      0x8
#define DDSD_PIXELFMT   0x1000
#define DDSD_MIPMAPCOUNT 0x20000
#define DDSD_LINEARSIZE 0x80000
#define DDPF_ALPHAPIXELS 0x1
#define DDPF_FOURCC     0x4
#define DDPF_RGB        0x40
#define DDSCAPS_TEXTURE 0x1000
#define DDSCAPS_COMPLEX 0x8
#define DDSCAPS_MIPMAP  0x400000

#define FOURCC_DXT1     0x31545844
#define FOURCC_DXT3     0x33545844
#define FOURCC_DXT5     0x35545844
#define FOURCC_ATI1     0x31495441
#define FOURCC_ATI2     0x32495441
#define FOURCC_BC7_     0x20374342
#define FOURCC_DX10     0x30315844

static inline void w32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }

static uint32_t fmt_to_fourcc(TexFormat f) {
    switch (f) {
        case TEX_FMT_BC1: return FOURCC_DXT1;
        case TEX_FMT_BC2: return FOURCC_DXT3;
        case TEX_FMT_BC3: return FOURCC_DXT5;
        case TEX_FMT_BC4: return FOURCC_ATI1;
        case TEX_FMT_BC5: return FOURCC_ATI2;
        default: return 0;
    }
}

uint8_t *dds_build(const TextureEntry *tex, size_t *out_size) {
    bool compressed = tex_format_is_compressed(tex->format);
    uint32_t fourcc = fmt_to_fourcc(tex->format);
    bool use_dx10 = compressed && !fourcc;
    bool uncompressed_argb = (tex->format == TEX_FMT_A8R8G8B8);

    size_t header_size = 4 + 124 + (use_dx10 ? 20 : 0);
    size_t total = header_size + tex->data_size;
    uint8_t *buf = (uint8_t *)calloc(1, total);
    if (!buf) return false;

    w32(buf, DDS_MAGIC);
    uint8_t *h = buf + 4;
    w32(h + 0, 124);

    uint32_t flags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFMT;
    if (compressed) flags |= DDSD_LINEARSIZE;
    else flags |= DDSD_PITCH;
    if (tex->mip_count > 1) flags |= DDSD_MIPMAPCOUNT;

    w32(h + 4, flags);
    w32(h + 8, tex->height);
    w32(h + 12, tex->width);
    w32(h + 16, compressed ? (uint32_t)tex_mip_data_size(tex->width, tex->height, tex->format)
                           : (uint32_t)tex_row_pitch(tex->width, tex->format));
    w32(h + 20, 1);
    w32(h + 24, tex->mip_count);
    w32(h + 72, 32);

    if (uncompressed_argb) {
        w32(h + 76, DDPF_RGB | DDPF_ALPHAPIXELS);
        w32(h + 84, 32);
        w32(h + 88, 0x00FF0000);
        w32(h + 92, 0x0000FF00);
        w32(h + 96, 0x000000FF);
        w32(h + 100, 0xFF000000);
    } else if (fourcc) {
        w32(h + 76, DDPF_FOURCC);
        w32(h + 80, fourcc);
    } else {
        w32(h + 76, DDPF_FOURCC);
        w32(h + 80, FOURCC_DX10);
    }

    uint32_t caps = DDSCAPS_TEXTURE;
    if (tex->mip_count > 1) caps |= DDSCAPS_COMPLEX | DDSCAPS_MIPMAP;
    w32(h + 104, caps);

    if (use_dx10) {
        uint8_t *dx = h + 124;
        uint32_t dxgi = 0;
        if (tex->format == TEX_FMT_BC7) dxgi = 98;
        w32(dx + 0, dxgi);
        w32(dx + 4, 3);
        w32(dx + 12, 1);
    }

    memcpy(buf + header_size, tex->data, tex->data_size);
    *out_size = total;
    return buf;
}
