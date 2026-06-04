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
    if (offset >= max) return NULL;
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

/* ── Write helpers + RSC7 (re)build for save ──────────────────────── */

static inline void wr16(uint8_t *p, uint16_t v) { memcpy(p, &v, 2); }
static inline void wr32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }
static inline void wr64(uint8_t *p, uint64_t v) { memcpy(p, &v, 8); }

static uint32_t format_to_dx9(TexFormat fmt) {
    switch (fmt) {
        case TEX_FMT_BC1:      return FOURCC_DXT1;
        case TEX_FMT_BC2:      return FOURCC_DXT3;
        case TEX_FMT_BC3:      return FOURCC_DXT5;
        case TEX_FMT_BC4:      return FOURCC_ATI1;
        case TEX_FMT_BC5:      return FOURCC_ATI2;
        case TEX_FMT_BC7:      return FOURCC_BC7;
        case TEX_FMT_A8R8G8B8: return 21;
        case TEX_FMT_A8:       return 28;
        case TEX_FMT_B5G5R5A1: return 25;
        case TEX_FMT_B5G6R5:   return 23;
        case TEX_FMT_R8:       return 50;
        default:               return 0;
    }
}

/* RSC7 page-flag encoding from a segment size (same scheme as ytd.c). */
/* See ytd.c for the rationale: keep the declared page (chunk) count small so it
 * never overflows the loader's fixed 128-entry chunk array (which manifests as
 * "address is neither virtual nor physical"). Pick the base page size that
 * minimises padding while staying within the chunk budget. */
#define RSC7_MAX_CHUNKS 128

static uint32_t rsc7_flags_from_size(size_t size, int version) {
    if (size == 0) return (uint32_t)((version & 0xF) << 28);
    static const int caps[]    = {1, 3, 15, 63, 127, 1, 1, 1, 1};
    static const int weights[] = {256, 128, 64, 32, 16, 8, 4, 2, 1};

    int    best_shift     = -1;
    size_t best_pad       = (size_t)-1;
    int    best_chunks    = 1 << 30;
    int    best_counts[9] = {0};

    for (int pass = 0; pass < 2 && best_shift < 0; pass++) {
        int chunk_cap = (pass == 0) ? (RSC7_MAX_CHUNKS / 2) : RSC7_MAX_CHUNKS;
        for (int base_shift = 0; base_shift <= 0xF; base_shift++) {
            size_t block_size  = (size_t)0x200 << base_shift;
            size_t rounded     = (size + block_size - 1) & ~(block_size - 1);
            size_t block_count = rounded / block_size;
            int counts[9] = {0};
            size_t remaining = block_count;
            for (int i = 0; i < 9; i++) {
                int take = (int)(remaining / weights[i]);
                if (take > caps[i]) take = caps[i];
                counts[i] = take;
                remaining -= (size_t)take * weights[i];
            }
            if (remaining != 0) continue;
            int chunks = 0;
            for (int i = 0; i < 9; i++) chunks += counts[i];
            if (chunks > chunk_cap) continue;
            if (best_shift < 0 || rounded < best_pad ||
                (rounded == best_pad && chunks < best_chunks)) {
                best_shift = base_shift; best_pad = rounded; best_chunks = chunks;
                for (int i = 0; i < 9; i++) best_counts[i] = counts[i];
            }
        }
    }
    if (best_shift < 0) return (uint32_t)((version & 0xF) << 28);

    uint32_t f = 0;
    f |= (uint32_t)((version & 0xF) << 28);
    f |= (uint32_t)((best_counts[8] & 1)    << 27);
    f |= (uint32_t)((best_counts[7] & 1)    << 26);
    f |= (uint32_t)((best_counts[6] & 1)    << 25);
    f |= (uint32_t)((best_counts[5] & 1)    << 24);
    f |= (uint32_t)((best_counts[4] & 0x7F) << 17);
    f |= (uint32_t)((best_counts[3] & 0x3F) << 11);
    f |= (uint32_t)((best_counts[2] & 0xF)  << 7);
    f |= (uint32_t)((best_counts[1] & 0x3)  << 5);
    f |= (uint32_t)((best_counts[0] & 0x1)  << 4);
    f |= (uint32_t)(best_shift & 0xF);
    return f;
}

static uint8_t *rsc7_compress(const uint8_t *data, size_t data_size, size_t *out_size) {
    size_t buf_size = data_size + (data_size / 8) + 256;
    uint8_t *out = (uint8_t *)malloc(buf_size);
    if (!out) return NULL;
    mz_stream stream;
    memset(&stream, 0, sizeof(stream));
    stream.next_in = data;
    stream.avail_in = (unsigned int)data_size;
    stream.next_out = out;
    stream.avail_out = (unsigned int)buf_size;
    if (mz_deflateInit2(&stream, MZ_BEST_COMPRESSION, MZ_DEFLATED,
                        -MZ_DEFAULT_WINDOW_BITS, 9, MZ_DEFAULT_STRATEGY) != MZ_OK) {
        free(out); return NULL;
    }
    if (mz_deflate(&stream, MZ_FINISH) != MZ_STREAM_END) {
        mz_deflateEnd(&stream); free(out); return NULL;
    }
    *out_size = stream.total_out;
    mz_deflateEnd(&stream);
    return out;
}

/* Retained original RSC7 payload + texture entry locations for write-back. */
typedef struct {
    uint8_t *vdata; size_t vsize;   /* system/virtual segment */
    uint8_t *pdata; size_t psize;   /* graphics/physical segment */
    uint32_t version;
    int      count;                 /* number of mapped texture entries (== texture_count) */
    size_t  *tex_off;               /* entry offset in vdata per texture */
    size_t  *data_off;              /* original data offset in pdata per texture */
} ModelMeta;

/* ── Parse texture dictionary from decompressed RSC7 data ─────────── */

static int parse_texdict(const uint8_t *vdata, size_t vdata_len,
                         const uint8_t *pdata, size_t pdata_len,
                         uint64_t dict_ptr, TextureEntry *out_textures, int max_textures,
                         size_t *out_tex_off, size_t *out_data_off) {
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
        if (tex_ptr < VIRTUAL_BASE) continue;
        size_t tex_off = (size_t)(tex_ptr - VIRTUAL_BASE);
        if (tex_off + GTAV_TEX_SIZE > vdata_len) continue;

        uint64_t name_ptr = rd64(vdata + tex_off + 0x28);
        if (name_ptr < VIRTUAL_BASE) continue;
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
        if (data_ptr < PHYSICAL_BASE) {
            free(name);
            continue;
        }
        size_t phys_off = (size_t)(data_ptr - PHYSICAL_BASE);
        if (phys_off > pdata_len || data_size > pdata_len - phys_off) {
            free(name);
            continue;
        }

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
            memcpy(te->data, pdata + phys_off, data_size);
        } else {
            free(name);
            continue;
        }
        if (out_tex_off)  out_tex_off[loaded]  = tex_off;   /* entry offset in vdata */
        if (out_data_off) out_data_off[loaded] = phys_off;  /* data offset in pdata  */
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
    if (drawable_ptr < VIRTUAL_BASE) return 0;
    size_t draw_off = (size_t)(drawable_ptr - VIRTUAL_BASE);
    if (draw_off + 0x18 > vdata_len) return 0;

    /* DrawableBase->ShaderGroupPointer is at offset 0x10
     * (0x10 ResourceFileBase header: VFT + Unknown + FilePagesInfoPointer). */
    uint64_t shader_group_ptr = rd64(vdata + draw_off + 0x10);
    if (shader_group_ptr < VIRTUAL_BASE) return 0;

    size_t sg_off = (size_t)(shader_group_ptr - VIRTUAL_BASE);
    if (sg_off + 0x10 > vdata_len) return 0;

    /* ShaderGroup->TextureDictionaryPointer is at offset 0x08
     * (0x00 VFT, 0x04 Unknown_4h, 0x08 TextureDictionaryPointer). */
    uint64_t texdict_ptr = rd64(vdata + sg_off + 0x08);
    if (texdict_ptr < VIRTUAL_BASE) return 0;

    return texdict_ptr;
}

/* ── Main loader ──────────────────────────────────────────────────── */

YtdFile *ydr_load(const wchar_t *filepath) {
    LOG("ydr_load: loading file");
    size_t file_size = 0;
    uint8_t *raw = read_file_bytes(filepath, &file_size);
    if (!raw || file_size < 16) { free(raw); SET_LOAD_ERR("file too small or unreadable"); return NULL; }

    uint32_t magic = rd32(raw);
    if (magic != RSC7_MAGIC) { free(raw); SET_LOAD_ERR("not an RSC7 resource (bad magic)"); return NULL; }

    uint32_t version = rd32(raw + 4);
    uint32_t sys_flags = rd32(raw + 8);
    uint32_t gfx_flags = rd32(raw + 12);

    size_t sys_size = rsc7_size_from_flags(sys_flags);
    size_t gfx_size = rsc7_size_from_flags(gfx_flags);
    size_t total = sys_size + gfx_size;
    if (total == 0) { free(raw); SET_LOAD_ERR("invalid resource size flags"); return NULL; }

    size_t decompressed_size = 0;
    uint8_t *payload = rsc7_decompress(raw + 16, file_size - 16, total, &decompressed_size);
    free(raw);
    if (!payload) { SET_LOAD_ERR("RSC7 decompression failed"); return NULL; }

    uint8_t *vdata = payload;
    size_t vdata_len = sys_size;
    uint8_t *pdata = payload + sys_size;
    size_t pdata_len = gfx_size;

    if (vdata_len < 0x40) { free(payload); SET_LOAD_ERR("system segment too small"); return NULL; }

    /* Determine file type by extension */
    const wchar_t *ext = wcsrchr(filepath, L'.');
    bool is_yft = (ext && _wcsicmp(ext, L".yft") == 0);
    bool is_ydd = (ext && _wcsicmp(ext, L".ydd") == 0);
    /* YDR is default */

    /* Allocate temp buffer for textures + parallel entry-offset arrays */
    TextureEntry *temp_textures = (TextureEntry *)calloc(EO_MAX_TEXTURES, sizeof(TextureEntry));
    size_t *temp_tex_off  = (size_t *)calloc(EO_MAX_TEXTURES, sizeof(size_t));
    size_t *temp_data_off = (size_t *)calloc(EO_MAX_TEXTURES, sizeof(size_t));
    if (!temp_textures || !temp_tex_off || !temp_data_off) {
        free(temp_textures); free(temp_tex_off); free(temp_data_off); free(payload); return NULL;
    }
    int total_loaded = 0;

    if (is_ydd) {
        /* YDD: Drawable Dictionary
         * +0x10: SimpleCollection<Drawable> pointer
         * +0x18: count/capacity
         * Each item is a pointer to a Drawable
         */
        /* DrawableDictionary: 0x30 DrawablesPointer, 0x38 DrawablesCount1. */
        uint64_t items_ptr = (vdata_len >= 0x38) ? rd64(vdata + 0x30) : 0;
        uint16_t count = (vdata_len >= 0x3A) ? rd16(vdata + 0x38) : 0;
        if (count > 0 && count < 1024 && items_ptr >= VIRTUAL_BASE) {
            size_t items_off = (size_t)(items_ptr - VIRTUAL_BASE);
            if (items_off + count * 8 <= vdata_len) {
                for (int i = 0; i < count && total_loaded < EO_MAX_TEXTURES; i++) {
                    uint64_t draw_ptr = rd64(vdata + items_off + i * 8);
                    uint64_t td = find_shader_group_texdict(vdata, vdata_len, draw_ptr);
                    if (td) {
                        int n = parse_texdict(vdata, vdata_len, pdata, pdata_len,
                                              td, temp_textures + total_loaded,
                                              EO_MAX_TEXTURES - total_loaded,
                                              temp_tex_off + total_loaded,
                                              temp_data_off + total_loaded);
                        total_loaded += n;
                    }
                }
            }
        }
    } else if (is_yft) {
        /* YFT: FragType (ResourceFileBase 0x10 header, then 0x10/0x18 unknown,
         * 0x20 BoundingSphereCenter+radius, 0x30 DrawablePointer).
         * The embedded textures live in the main FragDrawable's ShaderGroup. */
        if (vdata_len >= 0x38) {
            uint64_t draw_ptr = rd64(vdata + 0x30);
            uint64_t td = find_shader_group_texdict(vdata, vdata_len, draw_ptr);
            if (td) {
                total_loaded = parse_texdict(vdata, vdata_len, pdata, pdata_len,
                                             td, temp_textures, EO_MAX_TEXTURES,
                                             temp_tex_off, temp_data_off);
            }
        }
    } else {
        /* YDR: Single Drawable - root IS the drawable */
        uint64_t td = find_shader_group_texdict(vdata, vdata_len, VIRTUAL_BASE);
        if (td) {
            total_loaded = parse_texdict(vdata, vdata_len, pdata, pdata_len,
                                         td, temp_textures, EO_MAX_TEXTURES,
                                         temp_tex_off, temp_data_off);
        }
    }

    if (total_loaded == 0) {
        LOG("ydr_load: no embedded textures found");
        SET_LOAD_ERR("no embedded textures (model has no texture dictionary)");
        free(payload); free(temp_textures); free(temp_tex_off); free(temp_data_off);
        return NULL;
    }

    /* Build YtdFile from extracted textures */
    YtdFile *ytd = (YtdFile *)calloc(1, sizeof(YtdFile));
    if (!ytd) {
        for (int i = 0; i < total_loaded; i++) free(temp_textures[i].data);
        free(temp_textures); free(temp_tex_off); free(temp_data_off); free(payload);
        return NULL;
    }
    ytd->type = ARCHIVE_MODEL_READONLY;
    ytd->textures = (TextureEntry *)calloc(total_loaded, sizeof(TextureEntry));
    if (!ytd->textures) {
        for (int i = 0; i < total_loaded; i++) free(temp_textures[i].data);
        free(temp_textures); free(temp_tex_off); free(temp_data_off); free(payload);
        free(ytd);
        return NULL;
    }
    memcpy(ytd->textures, temp_textures, total_loaded * sizeof(TextureEntry));
    ytd->texture_count = total_loaded;

    /* Retain the original RSC7 payload + entry offsets so the model can be
     * recomposed on save (ydr_save). */
    ModelMeta *mm = (ModelMeta *)calloc(1, sizeof(ModelMeta));
    if (mm) {
        mm->tex_off  = (size_t *)malloc(total_loaded * sizeof(size_t));
        mm->data_off = (size_t *)malloc(total_loaded * sizeof(size_t));
        if (mm->tex_off && mm->data_off) {
            mm->vdata = payload;            /* owns the whole decompressed buffer */
            mm->vsize = sys_size;
            mm->pdata = payload + sys_size;
            mm->psize = gfx_size;
            mm->version = version;
            mm->count = total_loaded;
            memcpy(mm->tex_off,  temp_tex_off,  total_loaded * sizeof(size_t));
            memcpy(mm->data_off, temp_data_off, total_loaded * sizeof(size_t));
            ytd->model_meta = mm;
        } else {
            free(mm->tex_off); free(mm->data_off); free(mm);
            free(payload);     /* no meta: write-back unavailable, behaves read-only */
        }
    } else {
        free(payload);
    }

    const wchar_t *wfname = wcsrchr(filepath, L'\\');
    if (!wfname) wfname = wcsrchr(filepath, L'/');
    wfname = wfname ? wfname + 1 : filepath;
    WideCharToMultiByte(CP_UTF8, 0, wfname, -1, ytd->name, EO_MAX_NAME, NULL, NULL);
    wcsncpy(ytd->file_path, filepath, EO_MAX_PATH - 1);
    ytd->file_path[EO_MAX_PATH - 1] = 0;

    free(temp_textures); free(temp_tex_off); free(temp_data_off);
    LOG("ydr_load: loaded %d embedded textures from '%s'", total_loaded, ytd->name);
    return ytd;
}

/* ── Write-back: recompose YDR/YFT/YDD ────────────────────────────── */

void ydr_free_model_meta(YtdFile *archive) {
    if (!archive || !archive->model_meta) return;
    ModelMeta *mm = (ModelMeta *)archive->model_meta;
    free(mm->vdata);          /* the whole payload (vdata == start, pdata is inside) */
    free(mm->tex_off);
    free(mm->data_off);
    free(mm);
    archive->model_meta = NULL;
}

bool ydr_save(YtdFile *archive, const wchar_t *filepath) {
    if (!archive || !archive->model_meta) {
        SET_LOAD_ERR("model has no retained payload to recompose");
        return false;
    }
    ModelMeta *mm = (ModelMeta *)archive->model_meta;
    if (mm->count != archive->texture_count) {
        SET_LOAD_ERR("texture count changed; cannot recompose model");
        return false;
    }

    /* Work on a mutable copy of the system segment (entries get patched). */
    uint8_t *vbuf = (uint8_t *)malloc(mm->vsize);
    if (!vbuf) { SET_LOAD_ERR("out of memory"); return false; }
    memcpy(vbuf, mm->vdata, mm->vsize);

    /* Graphics segment starts as the original; changed textures are appended. */
    size_t pcap = mm->psize + 1024;
    size_t pcursor = mm->psize;        /* append point */
    uint8_t *pbuf = (uint8_t *)malloc(pcap);
    if (!pbuf) { free(vbuf); SET_LOAD_ERR("out of memory"); return false; }
    memcpy(pbuf, mm->pdata, mm->psize);

    for (int i = 0; i < mm->count; i++) {
        TextureEntry *te = &archive->textures[i];
        size_t off = mm->tex_off[i];
        if (off + GTAV_TEX_SIZE > mm->vsize) continue;

        /* Patch entry fields (dims / stride / format / mips). */
        wr16(vbuf + off + 0x50, (uint16_t)te->width);
        wr16(vbuf + off + 0x52, (uint16_t)te->height);
        wr16(vbuf + off + 0x56, (uint16_t)tex_row_pitch(te->width, te->format));
        wr32(vbuf + off + 0x58, format_to_dx9(te->format));
        vbuf[off + 0x5D] = (uint8_t)te->mip_count;

        /* Decide where the texture data lives. */
        size_t orig_size = 0;
        if (te->has_orig)
            orig_size = tex_total_mip_size(te->orig_width, te->orig_height, te->orig_format, te->orig_mip_count);

        bool edited = te->has_orig;
        if (!edited) {
            /* Unchanged: keep original data pointer/location. */
            wr64(vbuf + off + 0x70, PHYSICAL_BASE + mm->data_off[i]);
            continue;
        }

        if (te->data_size == orig_size && mm->data_off[i] + te->data_size <= mm->psize) {
            /* Same size: patch in place (structure identical to original). */
            memcpy(pbuf + mm->data_off[i], te->data, te->data_size);
            wr64(vbuf + off + 0x70, PHYSICAL_BASE + mm->data_off[i]);
        } else {
            /* Different size: append at a 16-aligned offset and repoint. */
            size_t aligned = (pcursor + 15) & ~(size_t)15;
            size_t need = aligned + te->data_size;
            if (need > pcap) {
                size_t ncap = need + need / 2 + 4096;
                uint8_t *np = (uint8_t *)realloc(pbuf, ncap);
                if (!np) { free(vbuf); free(pbuf); SET_LOAD_ERR("out of memory growing graphics segment"); return false; }
                pbuf = np; pcap = ncap;
            }
            if (aligned > pcursor) memset(pbuf + pcursor, 0, aligned - pcursor);
            memcpy(pbuf + aligned, te->data, te->data_size);
            wr64(vbuf + off + 0x70, PHYSICAL_BASE + aligned);
            pcursor = aligned + te->data_size;
        }
    }

    size_t new_psize = pcursor;

    /* Recompute page flags and pad each segment up to its flag-encoded size. */
    uint32_t sys_flags = rsc7_flags_from_size(mm->vsize, mm->version);
    uint32_t gfx_flags = rsc7_flags_from_size(new_psize, mm->version);
    size_t sys_target = rsc7_size_from_flags(sys_flags);
    size_t gfx_target = rsc7_size_from_flags(gfx_flags);

    uint8_t *vpad = (uint8_t *)calloc(1, sys_target ? sys_target : 1);
    uint8_t *ppad = (uint8_t *)calloc(1, gfx_target ? gfx_target : 1);
    if (!vpad || !ppad) { free(vbuf); free(pbuf); free(vpad); free(ppad); SET_LOAD_ERR("out of memory"); return false; }
    memcpy(vpad, vbuf, mm->vsize <= sys_target ? mm->vsize : sys_target);
    memcpy(ppad, pbuf, new_psize <= gfx_target ? new_psize : gfx_target);
    free(vbuf); free(pbuf);

    /* Concatenate, compress, write RSC7 header. */
    size_t combined_size = sys_target + gfx_target;
    uint8_t *combined = (uint8_t *)malloc(combined_size ? combined_size : 1);
    if (!combined) { free(vpad); free(ppad); SET_LOAD_ERR("out of memory"); return false; }
    memcpy(combined, vpad, sys_target);
    memcpy(combined + sys_target, ppad, gfx_target);
    free(vpad); free(ppad);

    size_t comp_size = 0;
    uint8_t *comp = rsc7_compress(combined, combined_size, &comp_size);
    free(combined);
    if (!comp) { SET_LOAD_ERR("RSC7 compression failed"); return false; }

    FILE *f = _wfopen(filepath, L"wb");
    if (!f) { free(comp); SET_LOAD_ERR("could not open output file for writing"); return false; }
    uint8_t header[16];
    wr32(header + 0, RSC7_MAGIC);
    wr32(header + 4, mm->version);
    wr32(header + 8, sys_flags);
    wr32(header + 12, gfx_flags);
    bool ok = fwrite(header, 1, 16, f) == 16 && fwrite(comp, 1, comp_size, f) == comp_size;
    fclose(f);
    free(comp);
    if (!ok) { SET_LOAD_ERR("failed writing model file"); return false; }

    LOG("ydr_save: recomposed '%ls' (sys=%zu gfx=%zu)", filepath, sys_target, gfx_target);
    return true;
}
