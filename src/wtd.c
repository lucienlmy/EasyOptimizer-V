#include "wtd.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MINIZ_HEADER_FILE_ONLY
#include "miniz.h"

#define VIRTUAL_BASE 0x50000000
#define PHYSICAL_BASE 0x60000000

static uint32_t read_u32(const uint8_t *data, int *pos) {
    uint32_t val = *(uint32_t*)(data + *pos);
    *pos += 4;
    return val;
}

static uint16_t read_u16(const uint8_t *data, int *pos) {
    uint16_t val = *(uint16_t*)(data + *pos);
    *pos += 2;
    return val;
}

static float read_float(const uint8_t *data, int *pos) {
    float val = *(float*)(data + *pos);
    *pos += 4;
    return val;
}

static TexFormat format_from_d3d(uint32_t format) {
    switch (format) {
        case 0x31545844: return TEX_FMT_BC1;
        case 0x33545844: return TEX_FMT_BC2;
        case 0x35545844: return TEX_FMT_BC3;
        case 0x31495441: return TEX_FMT_BC4;
        case 0x32495441: return TEX_FMT_BC5;
        case 21: return TEX_FMT_A8R8G8B8;
        case 25: return TEX_FMT_B5G5R5A1;
        case 23: return TEX_FMT_B5G6R5;
        case 28: return TEX_FMT_A8;
        case 50: return TEX_FMT_R8;
        default: return TEX_FMT_UNKNOWN;
    }
}

static uint32_t format_to_d3d(TexFormat format) {
    switch (format) {
        case TEX_FMT_BC1: return 0x31545844;
        case TEX_FMT_BC2: return 0x33545844;
        case TEX_FMT_BC3: return 0x35545844;
        case TEX_FMT_BC4: return 0x31495441;
        case TEX_FMT_BC5: return 0x32495441;
        case TEX_FMT_A8R8G8B8: return 21;
        case TEX_FMT_B5G5R5A1: return 25;
        case TEX_FMT_B5G6R5: return 23;
        case TEX_FMT_A8: return 28;
        case TEX_FMT_R8: return 50;
        default: return 0;
    }
}

YtdFile *wtd_load(const wchar_t *path) {
    FILE *f = _wfopen(path, L"rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size < 12) {
        fclose(f);
        return NULL;
    }

    uint8_t *fileData = malloc(file_size);
    if (!fileData) { fclose(f); LOG("[ERR] wtd_load: malloc(%ld) failed", file_size); return NULL; }
    if (fread(fileData, 1, file_size, f) != (size_t)file_size) { free(fileData); fclose(f); return NULL; }
    fclose(f);

    uint32_t magic = *(uint32_t*)fileData;
    if (magic != WTD_RSC5_MAGIC) {
        LOG("[ERR] wtd_load: Invalid magic 0x%08X", magic);
        free(fileData);
        return NULL;
    }

    uint32_t originalResourceType = *(uint32_t*)(fileData + 4);
    uint32_t flags = *(uint32_t*)(fileData + 8);

    uint32_t virtualSize = (flags & 0x7FFu) << (((flags >> 11) & 0xF) + 8);
    uint32_t physicalSize = ((flags >> 15) & 0x7FFu) << (((flags >> 26) & 0xF) + 8);

    uint32_t expected_decompressed = virtualSize + physicalSize;
    if (virtualSize < 32 || expected_decompressed < virtualSize) {
        free(fileData);
        return NULL;
    }
    uint8_t *decompressed = malloc(expected_decompressed);
    if (!decompressed) {
        free(fileData);
        return NULL;
    }

    mz_ulong dest_len = expected_decompressed;
    int mz_res = mz_uncompress(decompressed, &dest_len, fileData + 12, file_size - 12);
    free(fileData);

    if (mz_res != MZ_OK) {
        LOG("[ERR] wtd_load: mz_uncompress failed %d", mz_res);
        free(decompressed);
        return NULL;
    }

    uint8_t *virtualData = decompressed;
    uint8_t *physicalData = decompressed + virtualSize;

    int pos = 0;
    uint32_t originalVft = read_u32(virtualData, &pos);
    uint32_t originalBlockMapPtr = read_u32(virtualData, &pos);
    uint32_t originalParentDict = read_u32(virtualData, &pos);
    uint32_t originalUsageCount = read_u32(virtualData, &pos);

    uint32_t hashTablePtr = read_u32(virtualData, &pos);
    uint16_t hashCount = read_u16(virtualData, &pos);
    uint16_t hashCap = read_u16(virtualData, &pos);

    uint32_t texturesPtr = read_u32(virtualData, &pos);
    uint16_t texCount = read_u16(virtualData, &pos);
    uint16_t texCap = read_u16(virtualData, &pos);

    uint32_t *hashes = calloc(hashCount ? hashCount : 1, sizeof(uint32_t));
    if (!hashes) { free(decompressed); return NULL; }
    if (hashTablePtr != 0) {
        int hashOffset = hashTablePtr - VIRTUAL_BASE;
        if (hashOffset < 0 || hashOffset + hashCount * 4 > (int)virtualSize) {
            free(hashes);
            free(decompressed);
            return NULL;
        }
        for (int i = 0; i < hashCount; i++) {
            hashes[i] = *(uint32_t*)(virtualData + hashOffset + i * 4);
        }
    }

    uint32_t *texPtrs = calloc(texCount, sizeof(uint32_t));
    if (!texPtrs) { free(hashes); free(decompressed); return NULL; }
    if (texturesPtr != 0) {
        int ptrOffset = texturesPtr - VIRTUAL_BASE;
        if (ptrOffset < 0 || ptrOffset + texCount * 4 > (int)virtualSize) {
            free(texPtrs);
            free(hashes);
            free(decompressed);
            return NULL;
        }
        for (int i = 0; i < texCount; i++) {
            texPtrs[i] = *(uint32_t*)(virtualData + ptrOffset + i * 4);
        }
    }

    YtdFile *wtd = calloc(1, sizeof(YtdFile));
    if (!wtd) { free(texPtrs); free(hashes); free(decompressed); return NULL; }
    wcsncpy((wchar_t*)wtd->file_path, path, 1023);
    const wchar_t *slash = wcsrchr(wtd->file_path, L'\\');
    if (slash) {
        WideCharToMultiByte(CP_UTF8, 0, slash + 1, -1, wtd->name, 255, NULL, NULL);
    } else {
        WideCharToMultiByte(CP_UTF8, 0, path, -1, wtd->name, 255, NULL, NULL);
    }

    wtd->type = ARCHIVE_WTD;
    wtd->wtd_meta = calloc(1, sizeof(WtdFileMeta));
    WtdFileMeta *wtdm = wtd->wtd_meta;
    wtdm->original_resource_type = originalResourceType;
    wtdm->original_vft = originalVft;
    wtdm->original_block_map_ptr = originalBlockMapPtr;
    wtdm->original_parent_dict = originalParentDict;
    wtdm->original_usage_count = originalUsageCount;
    wtd->texture_count = 0;
    wtd->textures = calloc(texCount ? texCount : 1, sizeof(TextureEntry));
    if (!wtd->textures || !wtd->wtd_meta) {
        free(wtd->textures); free(wtd->wtd_meta); free(wtd);
        free(texPtrs); free(hashes); free(decompressed);
        return NULL;
    }

    for (int i = 0; i < texCount; i++) {
        if (texPtrs[i] == 0) continue;
        int texOffset = texPtrs[i] - VIRTUAL_BASE;
        if (texOffset < 0 || texOffset + 80 > (int)virtualSize) continue;

        TextureEntry *tex = &wtd->textures[wtd->texture_count++];
        tex->wtd_meta = calloc(1, sizeof(WtdTextureMetadata));
        if (!tex->wtd_meta) {
            wtd->texture_count--;
            continue;
        }
        WtdTextureMetadata *texm = tex->wtd_meta;
        texm->original_hash = (i < hashCount) ? hashes[i] : 0;

        pos = texOffset;
        texm->vft = read_u32(virtualData, &pos);
        texm->unknown1 = read_u32(virtualData, &pos);
        texm->unknown2 = read_u16(virtualData, &pos);
        texm->unknown3 = read_u16(virtualData, &pos);
        texm->unknown4 = read_u32(virtualData, &pos);
        texm->unknown5 = read_u32(virtualData, &pos);
        uint32_t namePtr = read_u32(virtualData, &pos);
        texm->unknown6 = read_u32(virtualData, &pos);

        tex->width = read_u16(virtualData, &pos);
        tex->height = read_u16(virtualData, &pos);
        tex->format = format_from_d3d(read_u32(virtualData, &pos));
        tex->stride = read_u16(virtualData, &pos);
        texm->texture_type = virtualData[pos++];
        tex->mip_count = virtualData[pos++];
        texm->unknown7 = read_float(virtualData, &pos);
        texm->unknown8 = read_float(virtualData, &pos);
        texm->unknown9 = read_float(virtualData, &pos);
        texm->unknown10 = read_float(virtualData, &pos);
        texm->unknown11 = read_float(virtualData, &pos);
        texm->unknown12 = read_float(virtualData, &pos);
        texm->prev_tex_offset = read_u32(virtualData, &pos);
        texm->next_tex_offset = read_u32(virtualData, &pos);
        uint32_t dataPtr = read_u32(virtualData, &pos);
        texm->unknown13 = read_u32(virtualData, &pos);

        if (namePtr != 0) {
            int nameOffset = namePtr - VIRTUAL_BASE;
            if (nameOffset >= 0 && nameOffset < (int)virtualSize) {
                const char *n = (const char*)(virtualData + nameOffset);
                strncpy(tex->name, n, 127);
                // Strip GTA4 prefix/suffixes
                if (_strnicmp(tex->name, "pack:/", 6) == 0) {
                    memmove(tex->name, tex->name + 6, strlen(tex->name) - 5);
                }
                char *dot = strrchr(tex->name, '.');
                if (dot && _stricmp(dot, ".dds") == 0) {
                    *dot = '\0';
                }
            }
        }

        if (tex->format != TEX_FMT_UNKNOWN && dataPtr != 0) {
            int dataOffset = dataPtr - PHYSICAL_BASE;
            if (dataOffset >= 0 && dataOffset < (int)physicalSize) {
                int dataSize = (int)tex_total_mip_size(tex->width, tex->height, tex->format, tex->mip_count);
                int available = physicalSize - dataOffset;
                if (dataSize > available) dataSize = available;
                
                tex->data_size = dataSize;
                tex->data = malloc(dataSize);
                if (tex->data)
                    memcpy(tex->data, physicalData + dataOffset, dataSize);
            }
        }
    }

    free(hashes);
    free(texPtrs);
    free(decompressed);

    return wtd;
}

static void write_u32(uint8_t *data, int offset, uint32_t value) {
    *(uint32_t*)(data + offset) = value;
}

static void write_u16(uint8_t *data, int offset, uint16_t value) {
    *(uint16_t*)(data + offset) = value;
}

static void write_float(uint8_t *data, int offset, float value) {
    *(float*)(data + offset) = value;
}

static int align16(int value) {
    return (value + 15) & ~15;
}

static uint32_t next_valid_rsc5_size(uint32_t size) {
    if (size == 0) return 256;
    for (int shift = 0; shift <= 15; shift++) {
        uint32_t unit = 1u << (shift + 8);
        uint32_t baseVal = (size + unit - 1) / unit;
        if (baseVal <= 0x7FF) return baseVal * unit;
    }
    return size;
}

static uint32_t encode_rsc5_flags(uint32_t virtualSize, uint32_t physicalSize) {
    uint32_t vBase = 0, vShift = 0;
    for (int s = 0; s <= 15; s++) {
        uint32_t unit = 1u << (s + 8);
        if (virtualSize % unit == 0) {
            uint32_t b = virtualSize / unit;
            if (b <= 0x7FF) { vBase = b; vShift = (uint32_t)s; }
        }
    }
    uint32_t pBase = 0, pShift = 0;
    for (int s = 0; s <= 15; s++) {
        uint32_t unit = 1u << (s + 8);
        if (physicalSize % unit == 0) {
            uint32_t b = physicalSize / unit;
            if (b <= 0x7FF) { pBase = b; pShift = (uint32_t)s; }
        }
    }
    return (vBase & 0x7FF) | ((vShift & 0xF) << 11) | ((pBase & 0x7FF) << 15) | ((pShift & 0xF) << 26);
}

// Jenkins one-at-a-time hash (often used in GTA)
static uint32_t jenk_hash(const char *key) {
    uint32_t hash = 0;
    while (*key) {
        hash += (uint8_t)*key++;
        hash += (hash << 10);
        hash ^= (hash >> 6);
    }
    hash += (hash << 3);
    hash ^= (hash >> 11);
    hash += (hash << 15);
    return hash;
}

static int hash_compare(const void *a, const void *b) {
    uint32_t ha = *(const uint32_t*)a;
    uint32_t hb = *(const uint32_t*)b;
    if (ha < hb) return -1;
    if (ha > hb) return 1;
    return 0;
}

bool wtd_save(const YtdFile *wtd, const wchar_t *path) {
    if (!wtd || wtd->texture_count == 0) return false;
    WtdFileMeta *wtdm = wtd->wtd_meta;
    if (!wtdm) return false;
    for (int i = 0; i < wtd->texture_count; i++) {
        if (!wtd->textures[i].data || wtd->textures[i].data_size == 0 ||
            !wtd->textures[i].wtd_meta || format_to_d3d(wtd->textures[i].format) == 0)
            return false;
    }

    int texCount = wtd->texture_count;
    int dictHeaderSize = 32;
    int hashArrayOffset = dictHeaderSize;
    int hashArraySize = texCount * 4;
    int ptrArrayOffset = hashArrayOffset + hashArraySize;
    int ptrArraySize = texCount * 4;
    int texStructsOffset = ptrArrayOffset + ptrArraySize;
    int texStructsSize = texCount * 80;
    int namesOffset = texStructsOffset + texStructsSize;

    int *nameOffsets = malloc(texCount * sizeof(int));
    int currentNameOffset = namesOffset;
    for (int i = 0; i < texCount; i++) {
        nameOffsets[i] = currentNameOffset;
        currentNameOffset += (int)strlen(wtd->textures[i].name) + 1; // null term
    }
    int virtualSize = align16(currentNameOffset);

    int *physicalOffsets = malloc(texCount * sizeof(int));
    int currentPhysOffset = 0;
    for (int i = 0; i < texCount; i++) {
        physicalOffsets[i] = currentPhysOffset;
        currentPhysOffset += (int)wtd->textures[i].data_size;
    }
    int physicalSize = align16(currentPhysOffset);

    uint32_t paddedVirtual = next_valid_rsc5_size(virtualSize);
    uint32_t paddedPhysical = next_valid_rsc5_size(physicalSize);

    uint8_t *virtualData = calloc(paddedVirtual, 1);
    if (!virtualData) { free(nameOffsets); free(physicalOffsets); return false; }
    uint8_t *physicalData = calloc(paddedPhysical, 1);
    if (!physicalData) { free(virtualData); free(nameOffsets); free(physicalOffsets); return false; }

    for (int i = 0; i < texCount; i++) {
        memcpy(physicalData + physicalOffsets[i], wtd->textures[i].data, wtd->textures[i].data_size);
        strcpy((char*)(virtualData + nameOffsets[i]), wtd->textures[i].name);
    }

    typedef struct {
        uint32_t hash;
        int original_index;
    } HashSortPair;

    HashSortPair *sorted = malloc(texCount * sizeof(HashSortPair));
    for (int i = 0; i < texCount; i++) {
        sorted[i].original_index = i;
        WtdTextureMetadata *texm = (WtdTextureMetadata*)wtd->textures[i].wtd_meta;
        if (texm && texm->original_hash != 0) {
            sorted[i].hash = texm->original_hash;
        } else {
            // Compute hash from lowercase name
            char lower_name[256];
            strncpy(lower_name, wtd->textures[i].name, 255);
            lower_name[255] = '\0';
            _strlwr(lower_name);
            sorted[i].hash = jenk_hash(lower_name);
        }
    }
    qsort(sorted, texCount, sizeof(HashSortPair), hash_compare);

    for (int i = 0; i < texCount; i++) {
        write_u32(virtualData, hashArrayOffset + i * 4, sorted[i].hash);
    }

    for (int i = 0; i < texCount; i++) {
        int srcIdx = sorted[i].original_index;
        uint32_t texStructAddr = VIRTUAL_BASE + texStructsOffset + i * 80;
        write_u32(virtualData, ptrArrayOffset + i * 4, texStructAddr);

        uint32_t prevAddr = (i > 0) ? (VIRTUAL_BASE + texStructsOffset + (i - 1) * 80) : 0;
        uint32_t nextAddr = (i < texCount - 1) ? (VIRTUAL_BASE + texStructsOffset + (i + 1) * 80) : 0;

        int pos = texStructsOffset + i * 80;
        const TextureEntry *tex = &wtd->textures[srcIdx];
        WtdTextureMetadata *texm = tex->wtd_meta;

        write_u32(virtualData, pos, texm->vft != 0 ? texm->vft : 0x00D50104);
        write_u32(virtualData, pos + 4, texm->unknown1);
        write_u16(virtualData, pos + 8, texm->unknown2);
        write_u16(virtualData, pos + 10, texm->unknown3);
        write_u32(virtualData, pos + 12, texm->unknown4);
        write_u32(virtualData, pos + 16, texm->unknown5);
        write_u32(virtualData, pos + 20, VIRTUAL_BASE + nameOffsets[srcIdx]);
        write_u32(virtualData, pos + 24, texm->unknown6);
        write_u16(virtualData, pos + 28, tex->width);
        write_u16(virtualData, pos + 30, tex->height);
        write_u32(virtualData, pos + 32, format_to_d3d(tex->format));
        
        uint16_t stride = tex->stride;
        if (tex_format_is_compressed(tex->format)) {
            int bw = (tex->width + 3) / 4;
            if (bw < 1) bw = 1;
            int bh = (tex->height + 3) / 4;
            if (bh < 1) bh = 1;
            int bs = tex_format_block_bytes(tex->format);
            int mip0 = bw * bh * bs;
            stride = (uint16_t)(mip0 / (tex->height > 0 ? tex->height : 1));
        }
        write_u16(virtualData, pos + 36, stride);
        
        virtualData[pos + 38] = texm->texture_type;
        virtualData[pos + 39] = tex->mip_count;
        write_float(virtualData, pos + 40, texm->unknown7);
        write_float(virtualData, pos + 44, texm->unknown8);
        write_float(virtualData, pos + 48, texm->unknown9);
        write_float(virtualData, pos + 52, texm->unknown10);
        write_float(virtualData, pos + 56, texm->unknown11);
        write_float(virtualData, pos + 60, texm->unknown12);
        write_u32(virtualData, pos + 64, prevAddr);
        write_u32(virtualData, pos + 68, nextAddr);
        write_u32(virtualData, pos + 72, PHYSICAL_BASE + physicalOffsets[srcIdx]);
        write_u32(virtualData, pos + 76, texm->unknown13);
    }

    int pos = 0;
    write_u32(virtualData, pos, wtdm->original_vft != 0 ? wtdm->original_vft : 0x00D6F028); pos += 4;
    write_u32(virtualData, pos, 0); pos += 4;                              /* BlockMapPtr (null) */
    write_u32(virtualData, pos, 0); pos += 4;                              /* ParentDict (runtime ptr, zeroed like C#) */
    write_u32(virtualData, pos, wtdm->original_usage_count); pos += 4;
    write_u32(virtualData, pos, VIRTUAL_BASE + hashArrayOffset); pos += 4;
    write_u16(virtualData, pos, texCount); pos += 2;
    write_u16(virtualData, pos, texCount); pos += 2;
    write_u32(virtualData, pos, VIRTUAL_BASE + ptrArrayOffset); pos += 4;
    write_u16(virtualData, pos, texCount); pos += 2;
    write_u16(virtualData, pos, texCount); pos += 2;

    uint32_t flags = encode_rsc5_flags(paddedVirtual, paddedPhysical);

    uint8_t *combined = malloc(paddedVirtual + paddedPhysical);
    memcpy(combined, virtualData, paddedVirtual);
    memcpy(combined + paddedVirtual, physicalData, paddedPhysical);

    mz_ulong cmp_len = mz_compressBound(paddedVirtual + paddedPhysical);
    uint8_t *cmp_data = malloc(cmp_len);
    
    int res = mz_compress(cmp_data, &cmp_len, combined, paddedVirtual + paddedPhysical);
    if (res != MZ_OK) {
        LOG("[ERR] wtd_save: mz_compress failed");
        free(virtualData); free(physicalData); free(sorted); free(nameOffsets); free(physicalOffsets); free(combined); free(cmp_data);
        return false;
    }

    FILE *f = _wfopen(path, L"wb");
    if (!f) {
        free(virtualData); free(physicalData); free(sorted); free(nameOffsets); free(physicalOffsets); free(combined); free(cmp_data);
        return false;
    }

    uint32_t header[3];
    header[0] = WTD_RSC5_MAGIC;
    header[1] = (wtdm && wtdm->original_resource_type != 0) ? wtdm->original_resource_type : 0x08;
    header[2] = flags;
    fwrite(header, 1, 12, f);
    fwrite(cmp_data, 1, cmp_len, f);
    fclose(f);

    free(virtualData); free(physicalData); free(sorted); free(nameOffsets); free(physicalOffsets); free(combined); free(cmp_data);
    return true;
}

void wtd_free(YtdFile *wtd) {
    if (!wtd) return;
    
    for (int i = 0; i < wtd->texture_count; i++) {
        free(wtd->textures[i].data);
        free(wtd->textures[i].wtd_meta);
    }
    free(wtd->textures);
    free(wtd->wtd_meta);
    free(wtd);
}
