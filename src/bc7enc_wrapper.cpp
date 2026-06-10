// C++ wrapper around bc7enc_rdo (with ISPC BC7) and stb libraries.
// Exposes a C API for the rest of the project.

#define SUPPORT_BC7E 1

#include "rdo_bc_encoder.h"
#include "bc7decomp.h"
#include "bc7e_ispc.h"
#include "eo_parallel.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"

#include "bc7enc_wrapper.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <algorithm>
#include <thread>
#include <vector>
#include <atomic>

static bool g_initialized = false;

extern "C" void bc7enc_init(void) {
    if (g_initialized) return;
    rgbcx::init();
    bc7enc_compress_block_init();
    ispc::bc7e_compress_block_init();
    g_initialized = true;
}

// ── Multithreaded block-level compression ──────────────────────────────────

struct CompressRegion {
    const uint8_t *src_rgba;
    uint8_t       *dst;
    int            width;
    int            block_row_start;
    int            block_row_end;
    int            height;
    int            fmt;
    int            bc1_quality;
    bool           use_hq;
    ispc::bc7e_compress_block_params bc7_params;
};

static void compress_region(const CompressRegion &r) {
    int bw = (r.width + 3) / 4;
    int block_bytes = (r.fmt == 0 || r.fmt == 2) ? 8 : 16;

    // 64 blocks at a time for optimal SIMD (matches rageAm approach)
    static const int GROUP_SIZE = 64;
    alignas(32) uint8_t block_buf[64 * 4 * 16]; // 64 blocks * 4x4 * 4 bytes

    for (int by = r.block_row_start; by < r.block_row_end; by++) {
        for (int bx_start = 0; bx_start < bw; bx_start += GROUP_SIZE) {
            int num_blocks = std::min(bw - bx_start, GROUP_SIZE);

            // Fill block buffer
            for (int b = 0; b < num_blocks; b++) {
                int bx = bx_start + b;
                for (int py = 0; py < 4; py++) {
                    int sy = by * 4 + py;
                    if (sy >= r.height) sy = r.height - 1;
                    for (int px = 0; px < 4; px++) {
                        int sx = bx * 4 + px;
                        if (sx >= r.width) sx = r.width - 1;
                        memcpy(&block_buf[(b * 16 + py * 4 + px) * 4],
                               &r.src_rgba[((size_t)sy * r.width + sx) * 4], 4);
                    }
                }
            }

            size_t dst_offset = ((size_t)by * bw + bx_start) * block_bytes;

            if (r.fmt == 4) {
                // BC7 — use ISPC accelerated encoder
                ispc::bc7e_compress_blocks(
                    num_blocks,
                    reinterpret_cast<uint64_t *>(r.dst + dst_offset),
                    reinterpret_cast<const uint32_t *>(block_buf),
                    &r.bc7_params);
            } else {
                // BC1/BC3/BC4/BC5 — use rgbcx per-block
                for (int b = 0; b < num_blocks; b++) {
                    uint8_t *src_block = &block_buf[b * 64];
                    uint8_t *dst_block = r.dst + dst_offset + (size_t)b * block_bytes;
                    switch (r.fmt) {
                        case 0: // BC1
                            rgbcx::encode_bc1(r.bc1_quality, dst_block, src_block, true, false);
                            break;
                        case 1: // BC3
                            if (r.use_hq)
                                rgbcx::encode_bc3_hq(r.bc1_quality, dst_block, src_block);
                            else
                                rgbcx::encode_bc3(r.bc1_quality, dst_block, src_block);
                            break;
                        case 2: // BC4
                            if (r.use_hq)
                                rgbcx::encode_bc4_hq(dst_block, src_block);
                            else
                                rgbcx::encode_bc4(dst_block, src_block);
                            break;
                        case 3: // BC5
                            if (r.use_hq)
                                rgbcx::encode_bc5_hq(dst_block, src_block);
                            else
                                rgbcx::encode_bc5(dst_block, src_block);
                            break;
                    }
                }
            }
        }
    }
}

static const int MAX_THREADS = 12;
static const int MIN_BLOCK_ROWS_PER_THREAD = 16;

extern "C" uint8_t *bc7enc_compress(const uint8_t *rgba, int w, int h, int fmt,
                                     float quality, size_t *out_size) {
    if (!rgba || w <= 0 || h <= 0) return NULL;
    if (!g_initialized) bc7enc_init();

    int bw = (w + 3) / 4;
    int bh = (h + 3) / 4;
    int block_bytes = (fmt == 0 || fmt == 2) ? 8 : 16;
    size_t total_size = (size_t)bw * bh * block_bytes;

    uint8_t *result = (uint8_t *)malloc(total_size);
    if (!result) return NULL;

    int bc1_quality = (int)(quality * rgbcx::MAX_LEVEL);
    if (bc1_quality > (int)rgbcx::MAX_LEVEL) bc1_quality = (int)rgbcx::MAX_LEVEL;
    bool use_hq = quality > 0.6f;

    // Setup BC7 ISPC params
    ispc::bc7e_compress_block_params bc7_params = {};
    if (fmt == 4) {
        int bc7_level = (int)(quality * 6.0f);
        if (bc7_level > 6) bc7_level = 6;
        switch (bc7_level) {
            case 0: ispc::bc7e_compress_block_params_init_ultrafast(&bc7_params, false); break;
            case 1: ispc::bc7e_compress_block_params_init_veryfast(&bc7_params, false); break;
            case 2: ispc::bc7e_compress_block_params_init_fast(&bc7_params, false); break;
            case 3: ispc::bc7e_compress_block_params_init_basic(&bc7_params, false); break;
            case 4: ispc::bc7e_compress_block_params_init_slow(&bc7_params, false); break;
            case 5: ispc::bc7e_compress_block_params_init_veryslow(&bc7_params, false); break;
            default: ispc::bc7e_compress_block_params_init_slowest(&bc7_params, false); break;
        }
    }

    // Determine thread count
    int num_threads = bh / MIN_BLOCK_ROWS_PER_THREAD;
    if (num_threads < 1) num_threads = 1;
    if (num_threads > MAX_THREADS) num_threads = MAX_THREADS;

    int hw_threads = (int)std::thread::hardware_concurrency();
    if (hw_threads > 0 && num_threads > hw_threads) num_threads = hw_threads;

    /* When an outer parallel-over-textures region is active, encode this texture
     * serially so the total thread count stays ~= core count (no nesting). */
    if (eo_inner_serial()) num_threads = 1;

    if (num_threads <= 1) {
        // Single-threaded path
        CompressRegion r = {};
        r.src_rgba = rgba;
        r.dst = result;
        r.width = w;
        r.height = h;
        r.block_row_start = 0;
        r.block_row_end = bh;
        r.fmt = fmt;
        r.bc1_quality = bc1_quality;
        r.use_hq = use_hq;
        r.bc7_params = bc7_params;
        compress_region(r);
    } else {
        // Multithreaded path
        int rows_per_thread = bh / num_threads;
        std::vector<std::thread> threads;
        std::vector<CompressRegion> regions(num_threads);

        for (int t = 0; t < num_threads; t++) {
            regions[t].src_rgba = rgba;
            regions[t].dst = result;
            regions[t].width = w;
            regions[t].height = h;
            regions[t].block_row_start = t * rows_per_thread;
            regions[t].block_row_end = (t == num_threads - 1) ? bh : (t + 1) * rows_per_thread;
            regions[t].fmt = fmt;
            regions[t].bc1_quality = bc1_quality;
            regions[t].use_hq = use_hq;
            regions[t].bc7_params = bc7_params;
            threads.emplace_back(compress_region, std::ref(regions[t]));
        }
        for (auto &th : threads) th.join();
    }

    *out_size = total_size;
    return result;
}

// ── Decompression ──────────────────────────────────────────────────────────

extern "C" int bc7enc_decompress_bc7_block(const uint8_t *block, uint8_t *rgba_out) {
    return bc7decomp::unpack_bc7(block, (bc7decomp::color_rgba *)rgba_out) ? 1 : 0;
}

extern "C" void bc7enc_decompress_bc1_block(const uint8_t *block, uint8_t *rgba_out) {
    rgbcx::unpack_bc1(block, rgba_out);
}

extern "C" void bc7enc_decompress_bc3_block(const uint8_t *block, uint8_t *rgba_out) {
    rgbcx::unpack_bc3(block, rgba_out);
}

extern "C" void bc7enc_decompress_bc4_block(const uint8_t *block, uint8_t *rgba_out, int stride) {
    rgbcx::unpack_bc4(block, rgba_out, (uint32_t)stride);
}

extern "C" void bc7enc_decompress_bc5_block(const uint8_t *block, uint8_t *rgba_out, int stride) {
    rgbcx::unpack_bc5(block, rgba_out, 0, 1, (uint32_t)stride);
}

// ── Image resize via stb_image_resize2 ─────────────────────────────────────

static stbir_filter map_filter(int f) {
    switch (f) {
        case 1: return STBIR_FILTER_BOX;
        case 2: return STBIR_FILTER_TRIANGLE;
        case 3: return STBIR_FILTER_CUBICBSPLINE;
        case 4: return STBIR_FILTER_CATMULLROM;
        case 5: return STBIR_FILTER_MITCHELL;
        case 6: return STBIR_FILTER_POINT_SAMPLE;
        default: return STBIR_FILTER_MITCHELL;
    }
}

extern "C" uint8_t *bc7enc_resize_rgba(const uint8_t *rgba, int src_w, int src_h,
                                        int dst_w, int dst_h, int filter) {
    uint8_t *out = (uint8_t *)malloc((size_t)dst_w * dst_h * 4);
    if (!out) return NULL;

    STBIR_RESIZE rs;
    stbir_resize_init(&rs, rgba, src_w, src_h, 0,
                      out, dst_w, dst_h, 0,
                      STBIR_RGBA, STBIR_TYPE_UINT8_SRGB);
    stbir_set_filters(&rs, map_filter(filter), map_filter(filter));
    stbir_resize_extended(&rs);

    return out;
}

// ── Image loading via stb_image ────────────────────────────────────────────

extern "C" uint8_t *bc7enc_load_image_mem(const uint8_t *data, size_t size,
                                           int *out_w, int *out_h) {
    int ch;
    return stbi_load_from_memory(data, (int)size, out_w, out_h, &ch, 4);
}

extern "C" uint8_t *bc7enc_load_image_file(const wchar_t *path, int *out_w, int *out_h) {
    FILE *f = _wfopen(path, L"rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    size_t sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = (uint8_t *)malloc(sz);
    if (!buf) { fclose(f); return NULL; }
    fread(buf, 1, sz, f);
    fclose(f);

    int ch;
    uint8_t *pixels = stbi_load_from_memory(buf, (int)sz, out_w, out_h, &ch, 4);
    free(buf);
    return pixels;
}

extern "C" void bc7enc_free(void *ptr) {
    free(ptr);
}
