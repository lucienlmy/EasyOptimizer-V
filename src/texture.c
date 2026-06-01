#include "texture.h"
#include "bc7enc_wrapper.h"
#include "nvtt_c_wrapper.h"
#include "gui.h"
#include <stdlib.h>
#include <string.h>

/* ── BC2 (DXT3) Decoder — not in bc7enc_rdo, keep our own ─────────── */

static void decode_bc2_alpha(const uint8_t *src, uint8_t *alpha16) {
    for (int y = 0; y < 4; y++) {
        uint16_t row = src[y*2] | (src[y*2+1] << 8);
        for (int x = 0; x < 4; x++) {
            uint8_t a4 = (row >> (x*4)) & 0xF;
            alpha16[y*4+x] = (uint8_t)(a4 * 255 / 15);
        }
    }
}

/* ── Generic block decoder ─────────────────────────────────────────── */

static uint8_t *decode_block_image(const uint8_t *data, int w, int h,
                                    int block_bytes, int fmt) {
    int bw = (w + 3) / 4;
    int bh = (h + 3) / 4;
    uint8_t *out = (uint8_t *)calloc(1, (size_t)w * h * 4);
    const uint8_t *src = data;

    for (int by = 0; by < bh; by++) {
        for (int bx = 0; bx < bw; bx++) {
            uint8_t rgba[16 * 4];
            memset(rgba, 0, sizeof(rgba));

            switch (fmt) {
                case TEX_FMT_BC1:
                    bc7enc_decompress_bc1_block(src, rgba);
                    break;
                case TEX_FMT_BC2: {
                    uint8_t alpha16[16];
                    decode_bc2_alpha(src, alpha16);
                    bc7enc_decompress_bc1_block(src + 8, rgba);
                    for (int i = 0; i < 16; i++)
                        rgba[i*4+3] = alpha16[i];
                    break;
                }
                case TEX_FMT_BC3:
                    bc7enc_decompress_bc3_block(src, rgba);
                    break;
                case TEX_FMT_BC4:
                    bc7enc_decompress_bc4_block(src, rgba, 4);
                    for (int i = 0; i < 16; i++) {
                        rgba[i*4+1] = rgba[i*4+0];
                        rgba[i*4+2] = rgba[i*4+0];
                        rgba[i*4+3] = 255;
                    }
                    break;
                case TEX_FMT_BC5:
                    bc7enc_decompress_bc5_block(src, rgba, 4);
                    for (int i = 0; i < 16; i++) {
                        rgba[i*4+2] = 255;
                        rgba[i*4+3] = 255;
                    }
                    break;
                case TEX_FMT_BC7:
                    bc7enc_decompress_bc7_block(src, rgba);
                    break;
                default:
                    break;
            }

            /* rgbcx outputs RGBA, we need BGRA for display */
            for (int py = 0; py < 4 && (by*4+py) < h; py++) {
                for (int px = 0; px < 4 && (bx*4+px) < w; px++) {
                    int si = (py * 4 + px) * 4;
                    int di = ((by*4+py) * w + (bx*4+px)) * 4;
                    out[di+0] = rgba[si+2]; /* B */
                    out[di+1] = rgba[si+1]; /* G */
                    out[di+2] = rgba[si+0]; /* R */
                    out[di+3] = rgba[si+3]; /* A */
                }
            }

            src += block_bytes;
        }
    }
    return out;
}

/* ── Public API ────────────────────────────────────────────────────── */

uint8_t *tex_decode_to_bgra(const TextureEntry *tex, int mip_level, int *out_w, int *out_h) {
    if (!tex || !tex->data) return NULL;

    int w = tex->width, h = tex->height;
    size_t offset = 0;
    for (int m = 0; m < mip_level && m < tex->mip_count; m++) {
        offset += tex_mip_data_size(w, h, tex->format);
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
    }
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    *out_w = w;
    *out_h = h;

    const uint8_t *mip_data = tex->data + offset;

    if (!tex_format_is_compressed(tex->format)) {
        size_t pixel_size = (size_t)w * h * 4;
        uint8_t *out = (uint8_t *)calloc(1, pixel_size);
        if (tex->format == TEX_FMT_A8R8G8B8) {
            memcpy(out, mip_data, pixel_size);
        } else if (tex->format == TEX_FMT_A8 || tex->format == TEX_FMT_R8) {
            for (int i = 0; i < w * h; i++) {
                out[i*4+0] = out[i*4+1] = out[i*4+2] = mip_data[i];
                out[i*4+3] = 255;
            }
        }
        return out;
    }

    int block_bytes = tex_format_block_bytes(tex->format);
    return decode_block_image(mip_data, w, h, block_bytes, tex->format);
}

/* ── Encoding via bc7enc_rdo ──────────────────────────────────────── */

static int tex_fmt_to_wrapper_fmt(TexFormat fmt) {
    switch (fmt) {
        case TEX_FMT_BC1: return 0;
        case TEX_FMT_BC3: return 1;
        case TEX_FMT_BC4: return 2;
        case TEX_FMT_BC5: return 3;
        case TEX_FMT_BC7: return 4;
        default: return -1;
    }
}

uint8_t *tex_encode_bc(const uint8_t *rgba, int w, int h, TexFormat fmt, size_t *out_size) {
    if (g_app.use_gpu_encoding) {
        NvttFormat nvttf;
        if (fmt == TEX_FMT_BC1) nvttf = NVTT_FORMAT_BC1;
        else if (fmt == TEX_FMT_BC3) nvttf = NVTT_FORMAT_BC3;
        else nvttf = NVTT_FORMAT_BC7;
        
        // bgra is needed, our rgba is actually BGRA internally from the decoder!
        // wait, let's verify if `rgba` is actually BGRA or RGBA.
        // In optimizer.c: `uint8_t *rgba = (uint8_t *)malloc(px_count * 4); ... rgba[p*4+0] = bgra[p*4+2]; ...`
        // Wait, optimizer.c converts to RGBA specifically for bc7enc.
        // NVTT needs BGRA (NVTT_INPUT_FORMAT_BGRA_8UB).
        // Let's just create a BGRA buffer.
        uint8_t *bgra = malloc((size_t)w * h * 4);
        for (int i = 0; i < w * h; i++) {
            bgra[i*4+0] = rgba[i*4+2];
            bgra[i*4+1] = rgba[i*4+1];
            bgra[i*4+2] = rgba[i*4+0];
            bgra[i*4+3] = rgba[i*4+3];
        }
        
        uint8_t *enc = NULL;
        bool ok = nvtt_encode(bgra, w, h, nvttf, &enc, out_size);
        free(bgra);
        if (ok && enc) return enc;
        
        // fallback to CPU
    }

    int wfmt = tex_fmt_to_wrapper_fmt(fmt);
    if (wfmt < 0) return NULL;
    return bc7enc_compress(rgba, w, h, wfmt, 1.0f, out_size);
}

/* ── Mip generation using stb_image_resize2 ──────────────────────── */

uint8_t *tex_generate_mips(const uint8_t *rgba, int w, int h, TexFormat fmt,
                           int max_mips, int *out_mip_count, size_t *out_total_size) {
    size_t total = 0;
    int mip_count = 0;
    int mw = w, mh = h;
    while (mw >= 1 && mh >= 1 && mip_count < max_mips) {
        total += tex_mip_data_size(mw, mh, fmt);
        mip_count++;
        if (mw == 1 && mh == 1) break;
        mw = mw > 1 ? mw / 2 : 1;
        mh = mh > 1 ? mh / 2 : 1;
    }

    uint8_t *result = (uint8_t *)malloc(total);
    size_t offset = 0;

    mw = w; mh = h;
    for (int m = 0; m < mip_count; m++) {
        const uint8_t *src = rgba;
        uint8_t *resized = NULL;

        if (m > 0) {
            resized = bc7enc_resize_rgba(rgba, w, h, mw, mh, 5);
            src = resized;
        }

        if (tex_format_is_compressed(fmt)) {
            size_t enc_size = 0;
            uint8_t *enc = tex_encode_bc(src, mw, mh, fmt, &enc_size);
            if (enc) {
                memcpy(result + offset, enc, enc_size);
                offset += enc_size;
                free(enc);
            }
        } else {
            size_t pix_size = tex_mip_data_size(mw, mh, fmt);
            if (fmt == TEX_FMT_A8R8G8B8) {
                for (int i = 0; i < mw * mh; i++) {
                    result[offset + i*4 + 0] = src[i*4+2];
                    result[offset + i*4 + 1] = src[i*4+1];
                    result[offset + i*4 + 2] = src[i*4+0];
                    result[offset + i*4 + 3] = src[i*4+3];
                }
                offset += pix_size;
            } else {
                memcpy(result + offset, src, pix_size);
                offset += pix_size;
            }
        }

        if (resized) bc7enc_free(resized);

        mw = mw > 1 ? mw / 2 : 1;
        mh = mh > 1 ? mh / 2 : 1;
    }

    *out_mip_count = mip_count;
    *out_total_size = offset;
    return result;
}
