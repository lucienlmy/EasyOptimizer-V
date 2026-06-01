/*
 * ydr.c - Extract embedded texture dictionaries from YDR/YFT/YDD files
 *
 * GTA V drawables (YDR), fragments (YFT), and drawable dictionaries (YDD)
 * are RSC7 resources that may contain an embedded TextureDictionary with
 * the same internal structure as a standalone YTD file.
 *
 * We decompress the RSC7 payload, locate the embedded texture dictionary
 * pointer within the drawable structure, and parse the textures using
 * the same logic as ytd_load.
 *
 * These files are loaded as read-only (ARCHIVE_YTD type) so existing
 * save/optimize/recompress features work on their textures. Saving back
 * to YDR/YFT/YDD is NOT supported - we export as YTD instead.
 */

#include "ydr.h"
#include "hash.h"
#include "log.h"
#include <windows.h>

#define RSC7_MAGIC       0x37435352
#define VIRTUAL_BASE     0x50000000ULL
#define PHYSICAL_BASE    0x60000000ULL
#define GTAV_TEX_SIZE    0x90

/* miniz */
#define MINIZ_NO_STDIO
#define MINIZ_NO_ARCHIVE_APIS
#define MINIZ_NO_ARCHIVE_WRITING_APIS
#define MINIZ_HEADER_FILE_ONLY
#include "miniz.h"

/* ── Forward declarations from ytd.c (shared helpers) ─────────────── */

/* We duplicate the minimal helpers we need to avoid exposing ytd.c internals */

static inline uint16_t rd16(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return v; }
static inline int16_t  rdi16(const uint8_t *p) { int16_t v; memcpy(&v, p, 2); return v; }
static inline uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static inline uint64_t rd64(const uint8_t *p) { uint64_t v; memcpy(&v, p, 8); return v; }

/* ── RSC7 flag helpers (same as ytd.c) ────────────────────────────── */

static size_t rsc7_size_from_flags(uint32_t flags) {
    int s0 = ((flags >> 27) & 1) << 0;
    int s1 = ((flags >> 26) & 1) << 1;
    int s2 = ((flags >> 25) & 1) << 2;
    int s3 = ((flags >> 24) & 1) << 3;
    int s4 = ((flags >> 17) & 0x7F) << 4;
    int s5 = ((flags >> 11) & 0x3F) << 5;
    int s6 = ((flags >> 7) & 0xF) << 6;
    int s7 = ((flags >> 5) & 0x3) << 7;
    int s8 = ((flags >> 4) & 0x1) << 8;
    int base_shift = flags & 0xF;
    size_t base_size = (size_t)0x200 << base_shift;
    return base_size * (s0 + s1 + s2 + s3 + s4 + s5 + s6 + s7 + s8);
}

/* ── DX9 format mapping ───────────────────────────────────────────── */

#define FOURCC_DXT1 0x31545844
#define FOURCC_DXT3 0x33545844
#define FOURCC_DXT5 0x35545844
#define FOURCC_ATI1 0x31495441
#define FOURCC_ATI2 0x32495441
#define FOURCC_BC7  0x20374342

static TexFormat resolve_dx9_format(uint32_t val) {
    switch (val) {
        case FOURCC_DXT1: return TEX_FMT_BC1;
        case FOURCC_DXT3: return TEX_FMT_BC2;
        case FOURCC_DXT5: return TEX_FMT_BC3;
        case FOURCC_ATI1: return TEX_FMT_BC4;
        case FOURCC_ATI2: return TEX_FMT_BC5;
        case FOURCC_BC7:  return TEX_FMT_BC7;
        case 21:          return TEX_FMT_A8R8G8B8;
        case 28:          return TEX_FMT_A8;
        case 25:          return TEX_FMT_B5G5R5A1;
        case 23:          return TEX_FMT_B5G6R5;
        case 50:          return TEX_FMT_R8;
        default:          return TEX_FMT_UNKNOWN;
    }
}

/* ── Helpers ───────────────────────────────────────────────────────── */

static char *read_cstring(const uint8_t *data, size_t offset, size_t max) {
    const uint8_t *start = data + offset;
    size_t len = 0;
    while (offset + len < max && start[len]) len++;
    char *s = (char *)malloc(len + 1);
    if (!s) return NULL;
    memcpy(s, start, len);
    s[len] = 0;
    return s;
}

static uint8_t *read_file_bytes(const wchar_t *path, size_t *out_size) {
    FILE *f = _wfopen(path, L"rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return NULL; }
    uint8_t *buf = (uint8_t *)malloc(sz);
    if (!buf) { fclose(f); return NULL; }
    fread(buf, 1, sz, f);
    fclose(f);
    *out_size = (size_t)sz;
    return buf;
}

static uint8_t *rsc7_decompress(const uint8_t *compressed, size_t comp_size,
                                 size_t expected_size, size_t *out_size) {
    uint8_t *out = (uint8_t *)malloc(expected_size);
    if (!out) return NULL;
    mz_stream stream;
    memset(&stream, 0, sizeof(stream));
    stream.next_in = compressed;
    stream.avail_in = (unsigned int)comp_size;
    stream.next_out = out;
    stream.avail_out = (unsigned int)expected_size;
    int ret = mz_inflateInit2(&stream, -MZ_DEFAULT_WINDOW_BITS);
    if (ret != MZ_OK) { free(out); return NULL; }
    ret = mz_inflate(&stream, MZ_FINISH);
    if (ret != MZ_STREAM_END && ret != MZ_OK) {
        mz_inflateEnd(&stream);
        free(out);
        return NULL;
    }
    *out_size = stream.total_out;
    mz_inflateEnd(&stream);
    return out;
}

/* ── Parse texture dictionary from decompressed RSC7 data ─────────── */

static int parse_texdict(const uint8_t *vdata, size_t vdata_len,
                         const uint8_t *pdata, size_t pdata_len,
                         uint64_t dict_ptr, TextureEntry *out_textures, int max_textures) {
    if (dict_ptr == 0) return 0;

    size_t dict_off = (size_t)(dict_ptr - VIRTUAL_BASE);
    if (dict_off + 0x40 > vdata_len) return 0;

    uint16_t count = rd16(vdata + dict_off + 0x28);
    uint64_t items_ptr = rd64(vdata + dict_off + 0x30);
    if (count == 0 || count > 4096) return 0;

    size_t items_off = (size_t)(items_ptr - VIRTUAL_BASE);
    if (items_off + count * 8 > vdata_len) return 0;

    int loaded = 0;
    for (int i = 0; i < count && loaded < max_textures; i++) {
        uint64_t tex_ptr = rd64(vdata + items_off + i * 8);
        size_t tex_off = (size_t)(tex_ptr - VIRTUAL_BASE);
        if (tex_off + GTAV_TEX_SIZE > vdata_len) continue;

        uint64_t name_ptr = rd64(vdata + tex_off + 0x28);
        size_t name_off = (size_t)(name_ptr - VIRTUAL_BASE);
        if (name_off >= vdata_len) continue;
        char *name = read_cstring(vdata, name_off, vdata_len);
        if (!name) continue;

        int16_t w = rdi16(vdata + tex_off + 0x50);
        int16_t h = rdi16(vdata + tex_off + 0x52);
        uint32_t fmt_val = rd32(vdata + tex_off + 0x58);
        uint8_t mip_count = vdata[tex_off + 0x5D];
        int16_t stride = rdi16(vdata + tex_off + 0x56);
        uint64_t data_ptr = rd64(vdata + tex_off + 0x70);

        TexFormat fmt = resolve_dx9_format(fmt_val);
        if (fmt == TEX_FMT_UNKNOWN || w <= 0 || h <= 0 || mip_count == 0) {
            free(name);
            continue;
        }

        size_t data_size = tex_total_mip_size(w, h, fmt, mip_count);
        size_t phys_off = (size_t)(data_ptr - PHYSICAL_BASE);

        TextureEntry *te = &out_textures[loaded];
        strncpy(te->name, name, EO_MAX_NAME - 1);
        te->name_hash = jenk_hash(name);
        te->width = w;
        te->height = h;
        te->format = fmt;
        te->mip_count = mip_count;
        te->stride = stride;
        te->data_size = data_size;
        te->data = (uint8_t *)malloc(data_size);
        if (te->data) {
            if (phys_off + data_size <= pdata_len)
                memcpy(te->data, pdata + phys_off, data_size);
            else
                memset(te->data, 0, data_size);
        }
        free(name);
        loaded++;
    }
    return loaded;
}

/* ── Locate embedded TextureDictionary ─────────────────────────────── */

/*
 * GTA V Drawable structure (simplified):
 *   +0x00: VFT pointer
 *   +0x20: ShaderGroup pointer -> +0x30: TextureDictionary pointer
 *
 * YDD (Drawable Dictionary):
 *   +0x00: VFT
 *   +0x10: pointer block / item count
 *   Contains multiple drawables, each potentially with embedded textures.
 *   We scan for all texture dictionaries.
 *
 * YFT (Fragment):
 *   +0x00: VFT
 *   +0x58: Drawable pointer -> same as YDR from there
 */

static uint64_t find_shader_group_texdict(const uint8_t *vdata, size_t vdata_len, uint64_t drawable_ptr) {
    if (drawable_ptr == 0) return 0;
    size_t draw_off = (size_t)(drawable_ptr - VIRTUAL_BASE);
    if (draw_off + 0x30 > vdata_len) return 0;

    /* Drawable->ShaderGroup is at offset 0x20 */
    uint64_t shader_group_ptr = rd64(vdata + draw_off + 0x20);
    if (shader_group_ptr == 0 || shader_group_ptr < VIRTUAL_BASE) return 0;

    size_t sg_off = (size_t)(shader_group_ptr - VIRTUAL_BASE);
    if (sg_off + 0x40 > vdata_len) return 0;

    /* ShaderGroup->TextureDictionary is at offset 0x30 */
    uint64_t texdict_ptr = rd64(vdata + sg_off + 0x30);
    if (texdict_ptr == 0 || texdict_ptr < VIRTUAL_BASE) return 0;

    return texdict_ptr;
}

/* ── Main loader ──────────────────────────────────────────────────── */

YtdFile *ydr_load(const wchar_t *filepath) {
    LOG("ydr_load: loading file");
    size_t file_size = 0;
    uint8_t *raw = read_file_bytes(filepath, &file_size);
    if (!raw || file_size < 16) { free(raw); return NULL; }

    uint32_t magic = rd32(raw);
    if (magic != RSC7_MAGIC) { free(raw); return NULL; }

    uint32_t version = rd32(raw + 4);
    uint32_t sys_flags = rd32(raw + 8);
    uint32_t gfx_flags = rd32(raw + 12);

    size_t sys_size = rsc7_size_from_flags(sys_flags);
    size_t gfx_size = rsc7_size_from_flags(gfx_flags);
    size_t total = sys_size + gfx_size;
    if (total == 0) { free(raw); return NULL; }

    size_t decompressed_size = 0;
    uint8_t *payload = rsc7_decompress(raw + 16, file_size - 16, total, &decompressed_size);
    free(raw);
    if (!payload) return NULL;

    uint8_t *vdata = payload;
    size_t vdata_len = sys_size;
    uint8_t *pdata = payload + sys_size;
    size_t pdata_len = gfx_size;

    if (vdata_len < 0x40) { free(payload); return NULL; }

    /* Determine file type by extension */
    const wchar_t *ext = wcsrchr(filepath, L'.');
    bool is_yft = (ext && _wcsicmp(ext, L".yft") == 0);
    bool is_ydd = (ext && _wcsicmp(ext, L".ydd") == 0);
    /* YDR is default */

    /* Allocate temp buffer for textures */
    TextureEntry *temp_textures = (TextureEntry *)calloc(EO_MAX_TEXTURES, sizeof(TextureEntry));
    if (!temp_textures) { free(payload); return NULL; }
    int total_loaded = 0;

    if (is_ydd) {
        /* YDD: Drawable Dictionary
         * +0x10: SimpleCollection<Drawable> pointer
         * +0x18: count/capacity
         * Each item is a pointer to a Drawable
         */
        uint64_t items_ptr = rd64(vdata + 0x30);
        uint16_t count = rd16(vdata + 0x28);
        if (count > 0 && count < 1024 && items_ptr >= VIRTUAL_BASE) {
            size_t items_off = (size_t)(items_ptr - VIRTUAL_BASE);
            if (items_off + count * 8 <= vdata_len) {
                for (int i = 0; i < count && total_loaded < EO_MAX_TEXTURES; i++) {
                    uint64_t draw_ptr = rd64(vdata + items_off + i * 8);
                    uint64_t td = find_shader_group_texdict(vdata, vdata_len, draw_ptr);
                    if (td) {
                        int n = parse_texdict(vdata, vdata_len, pdata, pdata_len,
                                              td, temp_textures + total_loaded,
                                              EO_MAX_TEXTURES - total_loaded);
                        total_loaded += n;
                    }
                }
            }
        }
    } else if (is_yft) {
        /* YFT: Fragment
         * +0x58: Drawable pointer (the main drawable)
         */
        if (vdata_len >= 0x60) {
            uint64_t draw_ptr = rd64(vdata + 0x58);
            uint64_t td = find_shader_group_texdict(vdata, vdata_len, draw_ptr);
            if (td) {
                total_loaded = parse_texdict(vdata, vdata_len, pdata, pdata_len,
                                             td, temp_textures, EO_MAX_TEXTURES);
            }
        }
    } else {
        /* YDR: Single Drawable - root IS the drawable */
        uint64_t td = find_shader_group_texdict(vdata, vdata_len, VIRTUAL_BASE);
        if (td) {
            total_loaded = parse_texdict(vdata, vdata_len, pdata, pdata_len,
                                         td, temp_textures, EO_MAX_TEXTURES);
        }
    }

    free(payload);

    if (total_loaded == 0) {
        LOG("ydr_load: no embedded textures found");
        free(temp_textures);
        return NULL;
    }

    /* Build YtdFile from extracted textures */
    YtdFile *ytd = (YtdFile *)calloc(1, sizeof(YtdFile));
    if (!ytd) { 
        for (int i = 0; i < total_loaded; i++) free(temp_textures[i].data);
        free(temp_textures); 
        return NULL; 
    }
    ytd->type = ARCHIVE_YTD; /* Treat as YTD for editing/saving */
    ytd->textures = (TextureEntry *)calloc(total_loaded, sizeof(TextureEntry));
    if (!ytd->textures) {
        for (int i = 0; i < total_loaded; i++) free(temp_textures[i].data);
        free(temp_textures);
        free(ytd);
        return NULL;
    }
    memcpy(ytd->textures, temp_textures, total_loaded * sizeof(TextureEntry));
    ytd->texture_count = total_loaded;

    const wchar_t *wfname = wcsrchr(filepath, L'\\');
    if (!wfname) wfname = wcsrchr(filepath, L'/');
    wfname = wfname ? wfname + 1 : filepath;
    WideCharToMultiByte(CP_UTF8, 0, wfname, -1, ytd->name, EO_MAX_NAME, NULL, NULL);
    wcsncpy(ytd->file_path, filepath, EO_MAX_PATH - 1);
    ytd->file_path[EO_MAX_PATH - 1] = 0;

    free(temp_textures);
    LOG("ydr_load: loaded %d embedded textures from '%s'", total_loaded, ytd->name);
    return ytd;
}
