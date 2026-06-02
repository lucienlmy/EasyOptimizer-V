#include "ytd.h"
#include "hash.h"
#include "log.h"
#include <windows.h>

#define RSC7_MAGIC       0x37435352
#define VIRTUAL_BASE     0x50000000ULL
#define PHYSICAL_BASE    0x60000000ULL
#define YTD_VERSION_LEGACY 13
#define YTD_VERSION_GEN9   5
#define GTAV_TEX_SIZE      0x90
#define GEN9_TEX_SIZE      0x80

/* miniz — C library for raw deflate (compiled in miniz_impl.c) */
#define MINIZ_NO_STDIO
#define MINIZ_NO_ARCHIVE_APIS
#define MINIZ_NO_ARCHIVE_WRITING_APIS
#define MINIZ_HEADER_FILE_ONLY
#include "miniz.h"

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

    /* -MZ_DEFAULT_WINDOW_BITS = raw deflate, same as Python zlib.decompress(data, -15) */
    int ret = mz_inflateInit2(&stream, -MZ_DEFAULT_WINDOW_BITS);
    if (ret != MZ_OK) { free(out); return NULL; }

    ret = mz_inflate(&stream, MZ_FINISH);
    if (ret != MZ_STREAM_END && ret != MZ_OK) {
        LOG_ERR("rsc7_decompress: mz_inflate returned %d", ret);
        mz_inflateEnd(&stream);
        free(out);
        return NULL;
    }

    *out_size = stream.total_out;
    mz_inflateEnd(&stream);
    return out;
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

    int ret = mz_deflateInit2(&stream, MZ_DEFAULT_LEVEL, MZ_DEFLATED,
                               -MZ_DEFAULT_WINDOW_BITS, 9, MZ_DEFAULT_STRATEGY);
    if (ret != MZ_OK) { free(out); return NULL; }

    ret = mz_deflate(&stream, MZ_FINISH);
    if (ret != MZ_STREAM_END) {
        LOG_ERR("rsc7_compress: mz_deflate returned %d", ret);
        mz_deflateEnd(&stream);
        free(out);
        return NULL;
    }

    *out_size = stream.total_out;
    mz_deflateEnd(&stream);
    return out;
}

/* ── RSC7 flag helpers ─────────────────────────────────────────────── */

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

static uint32_t rsc7_flags_from_size(size_t size, int version) {
    if (size == 0) return (uint32_t)((version & 0xF) << 28);
    size_t block_size = 0x200;
    while (1) {
        size_t rounded = (size + block_size - 1) & ~(block_size - 1);
        size_t block_count = rounded / block_size;
        int base_shift = 0;
        size_t bs = block_size;
        while (bs > 0x200) { base_shift++; bs >>= 1; }

        static const int caps[] = {1, 3, 15, 63, 127, 1, 1, 1, 1};
        static const int weights[] = {256, 128, 64, 32, 16, 8, 4, 2, 1};
        int counts[9] = {0};
        size_t remaining = block_count;
        bool ok = true;
        for (int i = 0; i < 9; i++) {
            int take = (int)(remaining / weights[i]);
            if (take > caps[i]) take = caps[i];
            counts[i] = take;
            remaining -= (size_t)take * weights[i];
        }
        if (remaining == 0) {
            uint32_t f = 0;
            f |= (uint32_t)((version & 0xF) << 28);
            f |= (uint32_t)((counts[8] & 1) << 27);
            f |= (uint32_t)((counts[7] & 1) << 26);
            f |= (uint32_t)((counts[6] & 1) << 25);
            f |= (uint32_t)((counts[5] & 1) << 24);
            f |= (uint32_t)((counts[4] & 0x7F) << 17);
            f |= (uint32_t)((counts[3] & 0x3F) << 11);
            f |= (uint32_t)((counts[2] & 0xF) << 7);
            f |= (uint32_t)((counts[1] & 0x3) << 5);
            f |= (uint32_t)((counts[0] & 0x1) << 4);
            f |= (uint32_t)(base_shift & 0xF);
            return f;
        }
        block_size <<= 1;
        if (block_size > ((size_t)0x200 << 0xF)) return (uint32_t)((version & 0xF) << 28);
    }
}

/* ── DX9 format mapping ────────────────────────────────────────────── */

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

/* ── DXGI format mapping (Gen9/Enhanced) ──────────────────────────── */

#define DXGI_BC1_UNORM       71
#define DXGI_BC1_UNORM_SRGB  72
#define DXGI_BC2_UNORM       74
#define DXGI_BC2_UNORM_SRGB  75
#define DXGI_BC3_UNORM       77
#define DXGI_BC3_UNORM_SRGB  78
#define DXGI_BC4_UNORM       80
#define DXGI_BC5_UNORM       83
#define DXGI_BC7_UNORM       98
#define DXGI_BC7_UNORM_SRGB  99
#define DXGI_R8G8B8A8_UNORM  28
#define DXGI_B8G8R8A8_UNORM  87
#define DXGI_R8_UNORM        61
#define DXGI_A8_UNORM        65
#define DXGI_B5G6R5_UNORM    85
#define DXGI_B5G5R5A1_UNORM  86

static TexFormat resolve_dxgi_format(uint32_t val) {
    switch (val) {
        case DXGI_BC1_UNORM: case DXGI_BC1_UNORM_SRGB: return TEX_FMT_BC1;
        case DXGI_BC2_UNORM: case DXGI_BC2_UNORM_SRGB: return TEX_FMT_BC2;
        case DXGI_BC3_UNORM: case DXGI_BC3_UNORM_SRGB: return TEX_FMT_BC3;
        case DXGI_BC4_UNORM:       return TEX_FMT_BC4;
        case DXGI_BC5_UNORM:       return TEX_FMT_BC5;
        case DXGI_BC7_UNORM: case DXGI_BC7_UNORM_SRGB: return TEX_FMT_BC7;
        case DXGI_R8G8B8A8_UNORM:  return TEX_FMT_R8G8B8A8;
        case DXGI_B8G8R8A8_UNORM:  return TEX_FMT_A8R8G8B8;
        case DXGI_R8_UNORM:        return TEX_FMT_R8;
        case DXGI_A8_UNORM:        return TEX_FMT_A8;
        case DXGI_B5G6R5_UNORM:    return TEX_FMT_B5G6R5;
        case DXGI_B5G5R5A1_UNORM:  return TEX_FMT_B5G5R5A1;
        default:                   return TEX_FMT_UNKNOWN;
    }
}

static uint32_t format_to_dxgi(TexFormat fmt) {
    switch (fmt) {
        case TEX_FMT_BC1:      return DXGI_BC1_UNORM;
        case TEX_FMT_BC2:      return DXGI_BC2_UNORM;
        case TEX_FMT_BC3:      return DXGI_BC3_UNORM;
        case TEX_FMT_BC4:      return DXGI_BC4_UNORM;
        case TEX_FMT_BC5:      return DXGI_BC5_UNORM;
        case TEX_FMT_BC7:      return DXGI_BC7_UNORM;
        case TEX_FMT_A8R8G8B8: return DXGI_B8G8R8A8_UNORM;
        case TEX_FMT_R8G8B8A8: return DXGI_R8G8B8A8_UNORM;
        case TEX_FMT_A8:       return DXGI_A8_UNORM;
        case TEX_FMT_R8:       return DXGI_R8_UNORM;
        case TEX_FMT_B5G6R5:   return DXGI_B5G6R5_UNORM;
        case TEX_FMT_B5G5R5A1: return DXGI_B5G5R5A1_UNORM;
        default:               return 0;
    }
}

/* ── Helpers ───────────────────────────────────────────────────────── */

static inline uint16_t rd16(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return v; }
static inline int16_t  rdi16(const uint8_t *p) { int16_t v; memcpy(&v, p, 2); return v; }
static inline uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static inline uint64_t rd64(const uint8_t *p) { uint64_t v; memcpy(&v, p, 8); return v; }

static inline void wr16(uint8_t *p, uint16_t v) { memcpy(p, &v, 2); }
static inline void wr32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }
static inline void wr64(uint8_t *p, uint64_t v) { memcpy(p, &v, 8); }

static size_t align_up(size_t v, size_t a) { return (v + a - 1) & ~(a - 1); }

static char *read_cstring(const uint8_t *data, size_t offset, size_t max) {
    const uint8_t *start = data + offset;
    size_t len = 0;
    while (offset + len < max && start[len]) len++;
    char *s = (char *)malloc(len + 1);
    memcpy(s, start, len);
    s[len] = 0;
    return s;
}

/* ── Read file ─────────────────────────────────────────────────────── */

static uint8_t *read_file_bytes(const wchar_t *path, size_t *out_size) {
    FILE *f = _wfopen(path, L"rb");
    if (!f) {
        LOG_ERR("read_file_bytes: failed to open file");
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return NULL; }
    uint8_t *buf = (uint8_t *)malloc(sz);
    if (!buf) { fclose(f); LOG_ERR("read_file_bytes: malloc(%ld) failed", sz); return NULL; }
    if (fread(buf, 1, sz, f) != (size_t)sz) { free(buf); fclose(f); return NULL; }
    fclose(f);
    *out_size = (size_t)sz;
    LOG("read_file_bytes: read %ld bytes", sz);
    return buf;
}

/* ── YTD Load (Legacy GTA V) ──────────────────────────────────────── */

YtdFile *ytd_load(const wchar_t *filepath) {
    LOG("ytd_load: loading file");
    size_t file_size = 0;
    uint8_t *raw = read_file_bytes(filepath, &file_size);
    if (!raw || file_size < 16) { LOG_ERR("ytd_load: file too small or unreadable"); SET_LOAD_ERR("file too small or unreadable"); free(raw); return NULL; }

    uint32_t magic = rd32(raw);
    if (magic != RSC7_MAGIC) { LOG_ERR("ytd_load: bad magic 0x%08X (expected 0x%08X)", magic, RSC7_MAGIC); SET_LOAD_ERR("not an RSC7 YTD (bad magic 0x%08X)", magic); free(raw); return NULL; }

    uint32_t version = rd32(raw + 4);
    uint32_t sys_flags = rd32(raw + 8);
    uint32_t gfx_flags = rd32(raw + 12);

    bool is_gen9 = (version == YTD_VERSION_GEN9);
    int tex_struct_size = is_gen9 ? GEN9_TEX_SIZE : GTAV_TEX_SIZE;
    LOG("ytd_load: version=%u %s sys_flags=0x%08X gfx_flags=0x%08X",
        version, is_gen9 ? "Gen9" : "Legacy", sys_flags, gfx_flags);

    size_t sys_size = rsc7_size_from_flags(sys_flags);
    size_t gfx_size = rsc7_size_from_flags(gfx_flags);
    size_t total = sys_size + gfx_size;
    LOG("ytd_load: sys_size=%zu gfx_size=%zu total=%zu", sys_size, gfx_size, total);

    if (total == 0) { LOG_ERR("ytd_load: computed total size is 0"); free(raw); return NULL; }

    size_t decompressed_size = 0;
    uint8_t *payload = rsc7_decompress(raw + 16, file_size - 16, total, &decompressed_size);
    free(raw);
    if (!payload) { LOG_ERR("ytd_load: rsc7_decompress failed (comp_size=%zu, expected=%zu)", file_size - 16, total); SET_LOAD_ERR("decompression failed (corrupt or unsupported)"); return NULL; }
    LOG("ytd_load: decompressed %zu bytes", decompressed_size);

    uint8_t *vdata = payload;
    size_t vdata_len = sys_size;
    uint8_t *pdata = payload + sys_size;
    size_t pdata_len = gfx_size;

    if (vdata_len < 0x40) { LOG_ERR("ytd_load: vdata_len too small (%zu)", vdata_len); free(payload); return NULL; }

    uint16_t count = rd16(vdata + 0x28);
    uint64_t items_ptr = rd64(vdata + 0x30);
    LOG("ytd_load: texture count=%u items_ptr=0x%llX", count, (unsigned long long)items_ptr);
    if (count == 0 || count > 4096) { LOG_ERR("ytd_load: invalid count %u", count); SET_LOAD_ERR("invalid texture count %u (maybe Gen9/enhanced format)", count); free(payload); return NULL; }

    if (items_ptr < VIRTUAL_BASE) { LOG_ERR("ytd_load: invalid items pointer"); free(payload); return NULL; }
    size_t items_off = (size_t)(items_ptr - VIRTUAL_BASE);
    if (items_off + count * 8 > vdata_len) { LOG_ERR("ytd_load: items_off out of bounds (%zu + %u*8 > %zu)", items_off, count, vdata_len); free(payload); return NULL; }

    YtdFile *ytd = (YtdFile *)calloc(1, sizeof(YtdFile));
    if (!ytd) { free(payload); return NULL; }
    ytd->type = ARCHIVE_YTD;
    ytd->textures = (TextureEntry *)calloc(count, sizeof(TextureEntry));
    if (!ytd->textures) { free(ytd); free(payload); return NULL; }
    ytd->texture_count = 0;

    const wchar_t *wfname = wcsrchr(filepath, L'\\');
    if (!wfname) wfname = wcsrchr(filepath, L'/');
    wfname = wfname ? wfname + 1 : filepath;
    WideCharToMultiByte(CP_UTF8, 0, wfname, -1, ytd->name, EO_MAX_NAME, NULL, NULL);
    wcsncpy(ytd->file_path, filepath, EO_MAX_PATH - 1);
    ytd->file_path[EO_MAX_PATH - 1] = 0;

    for (int i = 0; i < count; i++) {
        uint64_t tex_ptr = rd64(vdata + items_off + i * 8);
        if (tex_ptr < VIRTUAL_BASE) continue;
        size_t tex_off = (size_t)(tex_ptr - VIRTUAL_BASE);
        if (tex_off + tex_struct_size > vdata_len) continue;

        uint64_t name_ptr = rd64(vdata + tex_off + 0x28);
        if (name_ptr < VIRTUAL_BASE) continue;
        size_t name_off = (size_t)(name_ptr - VIRTUAL_BASE);
        if (name_off >= vdata_len) continue;
        char *name = read_cstring(vdata, name_off, vdata_len);
        if (!name) continue;

        int16_t w, h, stride;
        uint32_t fmt_val;
        uint8_t mip_count;
        uint64_t data_ptr;
        TexFormat fmt;

        if (is_gen9) {
            /* Gen9/Enhanced: 0x80 byte entries, DXGI format codes */
            w = rdi16(vdata + tex_off + 0x40);
            h = rdi16(vdata + tex_off + 0x42);
            fmt_val = rd32(vdata + tex_off + 0x48);
            mip_count = vdata[tex_off + 0x4D];
            stride = rdi16(vdata + tex_off + 0x46);
            data_ptr = rd64(vdata + tex_off + 0x60);
            fmt = resolve_dxgi_format(fmt_val);
        } else {
            /* Legacy: 0x90 byte entries, DX9 format codes */
            w = rdi16(vdata + tex_off + 0x50);
            h = rdi16(vdata + tex_off + 0x52);
            fmt_val = rd32(vdata + tex_off + 0x58);
            mip_count = vdata[tex_off + 0x5D];
            stride = rdi16(vdata + tex_off + 0x56);
            data_ptr = rd64(vdata + tex_off + 0x70);
            fmt = resolve_dx9_format(fmt_val);
        }

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

        TextureEntry *te = &ytd->textures[ytd->texture_count];
        strncpy(te->name, name, EO_MAX_NAME - 1);
        te->name_hash = jenk_hash(name);
        te->width = w;
        te->height = h;
        te->format = fmt;
        te->mip_count = mip_count;
        te->stride = stride;
        te->data_size = data_size;
        te->data = (uint8_t *)malloc(data_size);

        if (!te->data) {
            free(name);
            continue;
        }
        memcpy(te->data, pdata + phys_off, data_size);
        ytd->texture_count++;
        free(name);
    }

    free(payload);
    if (ytd->texture_count == 0) {
        free(ytd->textures);
        free(ytd);
        LOG_ERR("ytd_load: no valid textures");
        return NULL;
    }
    LOG("ytd_load: loaded %d textures from '%s'", ytd->texture_count, ytd->name);
    return ytd;
}

/* ── YTD Save (Legacy GTA V) ──────────────────────────────────────── */

typedef struct { int index; uint32_t hash; } SortEntry;
static int sort_by_hash(const void *a, const void *b) {
    uint32_t ha = ((SortEntry *)a)->hash;
    uint32_t hb = ((SortEntry *)b)->hash;
    return (ha > hb) - (ha < hb);
}

bool ytd_save(YtdFile *ytd, const wchar_t *filepath) {
    if (!ytd || ytd->texture_count == 0) { LOG_ERR("ytd_save: empty ytd"); return false; }
    for (int i = 0; i < ytd->texture_count; i++) {
        TextureEntry *te = &ytd->textures[i];
        if (!te->data || te->data_size == 0 || te->name[0] == 0 ||
            te->format == TEX_FMT_UNKNOWN || format_to_dx9(te->format) == 0) {
            LOG_ERR("ytd_save: invalid texture at index %d", i);
            return false;
        }
    }
    LOG("ytd_save: saving '%s' (%d textures)", ytd->name, ytd->texture_count);
    int count = ytd->texture_count;

    SortEntry *sorted = (SortEntry *)malloc(count * sizeof(SortEntry));
    for (int i = 0; i < count; i++) {
        sorted[i].index = i;
        sorted[i].hash = jenk_hash(ytd->textures[i].name);
    }
    qsort(sorted, count, sizeof(SortEntry), sort_by_hash);

    size_t dict_size = 0x40;
    size_t keys_off = dict_size;
    size_t ptrs_off = align_up(keys_off + 4 * count, 16);
    size_t textures_off = align_up(ptrs_off + 8 * count, 16);
    size_t current = textures_off + GTAV_TEX_SIZE * count;

    size_t *name_offs = (size_t *)malloc(count * sizeof(size_t));
    size_t *name_lens = (size_t *)malloc(count * sizeof(size_t));
    for (int i = 0; i < count; i++) {
        TextureEntry *te = &ytd->textures[sorted[i].index];
        name_offs[i] = current;
        name_lens[i] = strlen(te->name) + 1;
        current += name_lens[i];
    }

    size_t pagemap_off = align_up(current, 16);
    size_t virt_size = pagemap_off + 0x10;
    uint8_t *vbuf = (uint8_t *)calloc(1, virt_size);
    if (!vbuf) { free(sorted); free(name_offs); free(name_lens); return false; }

    size_t phys_cursor = 0;
    size_t *phys_offs = (size_t *)malloc(count * sizeof(size_t));
    for (int i = 0; i < count; i++) {
        TextureEntry *te = &ytd->textures[sorted[i].index];
        phys_offs[i] = phys_cursor;
        phys_cursor += te->data_size;
    }
    uint8_t *pbuf = (uint8_t *)calloc(1, phys_cursor ? phys_cursor : 1);
    if (!pbuf) { free(sorted); free(name_offs); free(name_lens); free(vbuf); free(phys_offs); return false; }

    /* TextureDictionary header */
    wr64(vbuf + 0x00, 0);
    wr64(vbuf + 0x08, VIRTUAL_BASE + pagemap_off);
    wr64(vbuf + 0x10, 0);
    wr32(vbuf + 0x18, 1);
    wr32(vbuf + 0x1C, 0);
    wr64(vbuf + 0x20, VIRTUAL_BASE + keys_off);
    wr16(vbuf + 0x28, (uint16_t)count);
    wr16(vbuf + 0x2A, (uint16_t)count);
    wr32(vbuf + 0x2C, 0);
    wr64(vbuf + 0x30, VIRTUAL_BASE + ptrs_off);
    wr16(vbuf + 0x38, (uint16_t)count);
    wr16(vbuf + 0x3A, (uint16_t)count);
    wr32(vbuf + 0x3C, 0);

    for (int i = 0; i < count; i++) {
        TextureEntry *te = &ytd->textures[sorted[i].index];
        wr32(vbuf + keys_off + i * 4, sorted[i].hash);
        wr64(vbuf + ptrs_off + i * 8, VIRTUAL_BASE + textures_off + GTAV_TEX_SIZE * i);

        size_t off = textures_off + GTAV_TEX_SIZE * i;
        uint32_t fmt_val = format_to_dx9(te->format);
        int stride = tex_row_pitch(te->width, te->format);

        size_t data_size_large = 0;
        for (int m = 0; m < te->mip_count; m++) {
            int mw = te->width >> m; if (mw < 1) mw = 1;
            int mh = te->height >> m; if (mh < 1) mh = 1;
            if (mw >= 16 && mh >= 16)
                data_size_large += tex_mip_data_size(mw, mh, te->format);
        }

        wr64(vbuf + off + 0x28, VIRTUAL_BASE + name_offs[i]);
        wr16(vbuf + off + 0x30, 1);
        wr32(vbuf + off + 0x40, (uint32_t)data_size_large);
        wr16(vbuf + off + 0x50, (uint16_t)te->width);
        wr16(vbuf + off + 0x52, (uint16_t)te->height);
        wr16(vbuf + off + 0x54, 1);
        wr16(vbuf + off + 0x56, (uint16_t)stride);
        wr32(vbuf + off + 0x58, fmt_val);
        vbuf[off + 0x5D] = (uint8_t)te->mip_count;
        wr64(vbuf + off + 0x70, PHYSICAL_BASE + phys_offs[i]);

        memcpy(vbuf + name_offs[i], te->name, name_lens[i]);
        if (te->data && te->data_size > 0)
            memcpy(pbuf + phys_offs[i], te->data, te->data_size);
    }

    vbuf[pagemap_off] = 1;
    vbuf[pagemap_off + 1] = 1;

    /* Build RSC7 */
    uint32_t sys_flags = rsc7_flags_from_size(virt_size, (YTD_VERSION_LEGACY >> 4) & 0xF);
    uint32_t gfx_flags = rsc7_flags_from_size(phys_cursor, YTD_VERSION_LEGACY & 0xF);
    size_t sys_target = rsc7_size_from_flags(sys_flags);
    size_t gfx_target = rsc7_size_from_flags(gfx_flags);

    size_t payload_size = sys_target + gfx_target;
    uint8_t *payload = (uint8_t *)calloc(1, payload_size);
    if (!payload) { free(sorted); free(name_offs); free(name_lens); free(vbuf); free(phys_offs); free(pbuf); return false; }
    memcpy(payload, vbuf, virt_size);
    if (phys_cursor > 0)
        memcpy(payload + sys_target, pbuf, phys_cursor);

    size_t comp_size = 0;
    uint8_t *compressed = rsc7_compress(payload, payload_size, &comp_size);

    free(payload);
    free(vbuf);
    free(pbuf);
    free(sorted);
    free(name_offs);
    free(name_lens);
    free(phys_offs);

    if (!compressed) return false;

    FILE *f = _wfopen(filepath, L"wb");
    if (!f) { LOG_ERR("ytd_save: failed to open output file"); free(compressed); return false; }

    uint8_t header[16];
    wr32(header, RSC7_MAGIC);
    wr32(header + 4, YTD_VERSION_LEGACY);
    wr32(header + 8, sys_flags);
    wr32(header + 12, gfx_flags);
    fwrite(header, 1, 16, f);
    fwrite(compressed, 1, comp_size, f);
    fclose(f);

    free(compressed);
    LOG("ytd_save: saved successfully (%zu bytes)", 16 + comp_size);
    return true;
}

void ytd_free(YtdFile *ytd) {
    if (!ytd) return;
    if (ytd->type == ARCHIVE_WTD) {
        extern void wtd_free(YtdFile *wtd);
        wtd_free(ytd);
        return;
    }
    for (int i = 0; i < ytd->texture_count; i++) {
        free(ytd->textures[i].data);
        free(ytd->textures[i].orig_data);
    }
    free(ytd->textures);
    free(ytd);
}
