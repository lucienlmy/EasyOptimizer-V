#include "texture.h"
#include "bc7enc_wrapper.h"
#include "nvtt_c_wrapper.h"
#include "eo_parallel.h"
#include "gui.h"
#include "log.h"
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

/* Decode context shared by every worker; each worker owns a disjoint block-row
 * range [by0, by1), reads its own slice of the source and writes disjoint output
 * rows, so no synchronization is needed. */
typedef struct {
    const uint8_t *data;
    uint8_t       *out;
    int w, h, bw, block_bytes, fmt;
} DecodeCtx;

static void decode_block_rows(size_t by0, size_t by1, void *vctx) {
    DecodeCtx *c = (DecodeCtx *)vctx;
    const int w = c->w, h = c->h, bw = c->bw, fmt = c->fmt;
    for (int by = (int)by0; by < (int)by1; by++) {
        const uint8_t *src = c->data + (size_t)by * bw * c->block_bytes;
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
                    c->out[di+0] = rgba[si+2]; /* B */
                    c->out[di+1] = rgba[si+1]; /* G */
                    c->out[di+2] = rgba[si+0]; /* R */
                    c->out[di+3] = rgba[si+3]; /* A */
                }
            }
            src += c->block_bytes;
        }
    }
}

static uint8_t *decode_block_image(const uint8_t *data, int w, int h,
                                    int block_bytes, int fmt) {
    int bw = (w + 3) / 4;
    int bh = (h + 3) / 4;
    uint8_t *out = (uint8_t *)calloc(1, (size_t)w * h * 4);
    if (!out) return NULL;   /* huge/corrupt dimensions: fail gracefully */

    DecodeCtx c = { data, out, w, h, bw, block_bytes, fmt };
    /* One worker per ~16 block-rows, capped at the hardware thread count, so
     * small thumbnails stay single-threaded (avoids spawn overhead). Inside an
     * outer parallel-over-textures region we force serial to avoid nesting. */
    int nt = eo_inner_serial() ? 1 : (bh / 16);
    if (nt < 1) nt = 1;
    eo_parallel_range(0, (size_t)bh, decode_block_rows, &c, nt);
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

    size_t mip_size = tex_mip_data_size(w, h, tex->format);
    if (offset > tex->data_size || mip_size > tex->data_size - offset) return NULL;
    const uint8_t *mip_data = tex->data + offset;

    if (!tex_format_is_compressed(tex->format)) {
        size_t pixel_size = (size_t)w * h * 4;
        uint8_t *out = (uint8_t *)calloc(1, pixel_size);
        if (!out) return NULL;   /* huge/corrupt dimensions: fail gracefully */
        if (tex->format == TEX_FMT_A8R8G8B8) {
            memcpy(out, mip_data, pixel_size);
        } else if (tex->format == TEX_FMT_R8G8B8A8) {
            for (int i = 0; i < w * h; i++) {
                out[i*4+0] = mip_data[i*4+2];
                out[i*4+1] = mip_data[i*4+1];
                out[i*4+2] = mip_data[i*4+0];
                out[i*4+3] = mip_data[i*4+3];
            }
        } else if (tex->format == TEX_FMT_A8 || tex->format == TEX_FMT_R8) {
            for (int i = 0; i < w * h; i++) {
                out[i*4+0] = out[i*4+1] = out[i*4+2] = mip_data[i];
                out[i*4+3] = 255;
            }
        } else if (tex->format == TEX_FMT_B5G6R5 || tex->format == TEX_FMT_B5G5R5A1) {
            for (int i = 0; i < w * h; i++) {
                uint16_t packed = (uint16_t)(mip_data[i*2] | (mip_data[i*2+1] << 8));
                if (tex->format == TEX_FMT_B5G6R5) {
                    out[i*4+0] = (uint8_t)(((packed >> 0) & 0x1F) * 255 / 31);
                    out[i*4+1] = (uint8_t)(((packed >> 5) & 0x3F) * 255 / 63);
                    out[i*4+2] = (uint8_t)(((packed >> 11) & 0x1F) * 255 / 31);
                    out[i*4+3] = 255;
                } else {
                    out[i*4+0] = (uint8_t)(((packed >> 0) & 0x1F) * 255 / 31);
                    out[i*4+1] = (uint8_t)(((packed >> 5) & 0x1F) * 255 / 31);
                    out[i*4+2] = (uint8_t)(((packed >> 10) & 0x1F) * 255 / 31);
                    out[i*4+3] = (packed & 0x8000) ? 255 : 0;
                }
            }
        }
        return out;
    }

    int block_bytes = tex_format_block_bytes(tex->format);
    return decode_block_image(mip_data, w, h, block_bytes, tex->format);
}

/* ── Encoding via bc7enc_rdo ──────────────────────────────────────── */

bool tex_format_can_encode(TexFormat fmt) {
    return fmt == TEX_FMT_BC1 || fmt == TEX_FMT_BC2 || fmt == TEX_FMT_BC3 || fmt == TEX_FMT_BC4 ||
           fmt == TEX_FMT_BC5 || fmt == TEX_FMT_BC7 || fmt == TEX_FMT_A8R8G8B8 ||
           fmt == TEX_FMT_R8G8B8A8 || fmt == TEX_FMT_B5G6R5 ||
           fmt == TEX_FMT_B5G5R5A1 || fmt == TEX_FMT_A8 || fmt == TEX_FMT_R8;
}

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

static uint8_t *tex_encode_bc2(const uint8_t *rgba, int w, int h, size_t *out_size) {
    size_t bc1_size = 0;
    uint8_t *bc1 = bc7enc_compress(rgba, w, h, 0, 1.0f, &bc1_size);
    if (!bc1) return NULL;

    int bw = (w + 3) / 4;
    int bh = (h + 3) / 4;
    size_t total = (size_t)bw * bh * 16;
    uint8_t *result = (uint8_t *)malloc(total);
    if (!result) {
        free(bc1);
        return NULL;
    }

    for (int by = 0; by < bh; by++) {
        for (int bx = 0; bx < bw; bx++) {
            size_t block = (size_t)by * bw + bx;
            uint8_t *dst = result + block * 16;
            for (int py = 0; py < 4; py++) {
                uint16_t row = 0;
                int sy = by * 4 + py;
                if (sy >= h) sy = h - 1;
                for (int px = 0; px < 4; px++) {
                    int sx = bx * 4 + px;
                    if (sx >= w) sx = w - 1;
                    uint8_t alpha = rgba[((size_t)sy * w + sx) * 4 + 3];
                    row |= (uint16_t)(alpha >> 4) << (px * 4);
                }
                dst[py*2] = (uint8_t)row;
                dst[py*2+1] = (uint8_t)(row >> 8);
            }
            memcpy(dst + 8, bc1 + block * 8, 8);
        }
    }
    free(bc1);
    *out_size = total;
    return result;
}

/* Log the effective encoder only when it changes, so batches don't spam. */
static void log_encoder_used(const char *mode) {
    static const char *last = NULL;
    if (mode != last) {
        LOG("encoder: using %s", mode);
        last = mode;
    }
}

uint8_t *tex_encode_bc(const uint8_t *rgba, int w, int h, TexFormat fmt, size_t *out_size) {
    if (fmt == TEX_FMT_BC2) {
        log_encoder_used("CPU (bc7enc ISPC)");
        return tex_encode_bc2(rgba, w, h, out_size);
    }
    // NVTT 3.2.5 handles BC1, BC3 and BC7, including edge blocks.
    if (g_app.use_gpu_encoding && (fmt == TEX_FMT_BC1 || fmt == TEX_FMT_BC3 || fmt == TEX_FMT_BC7)) {
        NvttFormat nvttf = (fmt == TEX_FMT_BC1) ? NVTT_FORMAT_BC1 :
                           (fmt == TEX_FMT_BC3) ? NVTT_FORMAT_BC3 : NVTT_FORMAT_BC7;

        // NVTT wants BGRA; our `rgba` buffer is RGBA, so swap channels.
        uint8_t *bgra = malloc((size_t)w * h * 4);
        if (bgra) {
            for (int i = 0; i < w * h; i++) {
                bgra[i*4+0] = rgba[i*4+2];
                bgra[i*4+1] = rgba[i*4+1];
                bgra[i*4+2] = rgba[i*4+0];
                bgra[i*4+3] = rgba[i*4+3];
            }
            uint8_t *enc = NULL;
            bool ok = nvtt_encode(bgra, w, h, nvttf, &enc, out_size);
            free(bgra);
            if (ok && enc) {
                log_encoder_used("GPU (NVTT CUDA)");
                return enc;
            }
        }
        // GPU requested but unavailable/failed → fall through to CPU.
        log_encoder_used("CPU (bc7enc ISPC) [GPU fallback]");
    } else {
        log_encoder_used("CPU (bc7enc ISPC)");
    }

    int wfmt = tex_fmt_to_wrapper_fmt(fmt);
    if (wfmt < 0) return NULL;
    return bc7enc_compress(rgba, w, h, wfmt, 1.0f, out_size);
}

/* ── Mip generation using stb_image_resize2 ──────────────────────── */

uint8_t *tex_generate_mips(const uint8_t *rgba, int w, int h, TexFormat fmt,
                           int max_mips, int *out_mip_count, size_t *out_total_size) {
    if (!rgba || w <= 0 || h <= 0 || max_mips <= 0 || !tex_format_can_encode(fmt))
        return NULL;
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
    if (!result) return NULL;
    size_t offset = 0;

    mw = w; mh = h;
    for (int m = 0; m < mip_count; m++) {
        const uint8_t *src = rgba;
        uint8_t *resized = NULL;

        if (m > 0) {
            resized = bc7enc_resize_rgba(rgba, w, h, mw, mh, 5);
            if (!resized) {
                free(result);
                return NULL;
            }
            src = resized;
        }

        if (tex_format_is_compressed(fmt)) {
            size_t enc_size = 0;
            uint8_t *enc = tex_encode_bc(src, mw, mh, fmt, &enc_size);
            size_t expected_size = tex_mip_data_size(mw, mh, fmt);
            if (!enc || enc_size != expected_size) {
                free(enc);
                if (resized) bc7enc_free(resized);
                free(result);
                return NULL;
            }
            memcpy(result + offset, enc, enc_size);
            offset += enc_size;
            free(enc);
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
            } else if (fmt == TEX_FMT_R8G8B8A8) {
                memcpy(result + offset, src, pix_size);
                offset += pix_size;
            } else if (fmt == TEX_FMT_A8 || fmt == TEX_FMT_R8) {
                for (int i = 0; i < mw * mh; i++)
                    result[offset + i] = (fmt == TEX_FMT_A8) ? src[i*4+3] : src[i*4+0];
                offset += pix_size;
            } else if (fmt == TEX_FMT_B5G6R5 || fmt == TEX_FMT_B5G5R5A1) {
                for (int i = 0; i < mw * mh; i++) {
                    uint16_t packed;
                    if (fmt == TEX_FMT_B5G6R5) {
                        packed = (uint16_t)(((src[i*4+0] * 31 / 255) << 11) |
                                            ((src[i*4+1] * 63 / 255) << 5) |
                                            (src[i*4+2] * 31 / 255));
                    } else {
                        packed = (uint16_t)(((src[i*4+3] >= 128 ? 1 : 0) << 15) |
                                            ((src[i*4+0] * 31 / 255) << 10) |
                                            ((src[i*4+1] * 31 / 255) << 5) |
                                            (src[i*4+2] * 31 / 255));
                    }
                    result[offset + i*2] = (uint8_t)packed;
                    result[offset + i*2+1] = (uint8_t)(packed >> 8);
                }
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

void tex_save_original(TextureEntry *te) {
    if (!te || te->has_orig) return;            /* snapshot only once */
    if (!te->data || te->data_size == 0) return;
    te->orig_data = (uint8_t *)malloc(te->data_size);
    if (!te->orig_data) return;                 /* OOM: skip snapshot, edit still proceeds */
    memcpy(te->orig_data, te->data, te->data_size);
    te->orig_data_size = te->data_size;
    te->orig_width = te->width;
    te->orig_height = te->height;
    te->orig_format = te->format;
    te->orig_mip_count = te->mip_count;
    te->orig_stride = te->stride;
    te->has_orig = true;
}

bool tex_revert_original(TextureEntry *te) {
    if (!te || !te->has_orig) return false;
    free(te->data);
    te->data = te->orig_data;                   /* transfer ownership back (no copy) */
    te->data_size = te->orig_data_size;
    te->width = te->orig_width;
    te->height = te->orig_height;
    te->format = te->orig_format;
    te->mip_count = te->orig_mip_count;
    te->stride = te->orig_stride;
    te->orig_data = NULL;
    te->orig_data_size = 0;
    te->has_orig = false;
    return true;
}

void tex_free_original(TextureEntry *te) {
    if (!te) return;
    free(te->orig_data);
    te->orig_data = NULL;
    te->orig_data_size = 0;
    te->has_orig = false;
}

bool tex_alpha_in_use(const TextureEntry *tex) {
    /* Formats without an alpha channel: BC1 has 1-bit punch-through but
     * we still treat it as "no alpha" for downgrade purposes. */
    if (tex->format == TEX_FMT_BC1 || tex->format == TEX_FMT_BC4 ||
        tex->format == TEX_FMT_BC5 || tex->format == TEX_FMT_R8 ||
        tex->format == TEX_FMT_B5G6R5) {
        return false;
    }
    int w = 0, h = 0;
    uint8_t *bgra = tex_decode_to_bgra(tex, 0, &w, &h);
    if (!bgra) return true; /* safe default: assume has alpha */
    bool has_alpha = false;
    size_t pixels = (size_t)w * h;
    for (size_t i = 0; i < pixels; i++) {
        if (bgra[i*4+3] < 250) { has_alpha = true; break; }
    }
    free(bgra);
    return has_alpha;
}
