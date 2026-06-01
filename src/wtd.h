#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "types.h"

#define WTD_RSC5_MAGIC 0x05435352

/* GTA IV WTD uses RSC5 struct metadata */
typedef struct {
    uint32_t vft;
    uint32_t unknown1;
    uint16_t unknown2;
    uint16_t unknown3;
    uint32_t unknown4;
    uint32_t unknown5;
    uint32_t unknown6;
    uint8_t  texture_type;
    float    unknown7;
    float    unknown8;
    float    unknown9;
    float    unknown10;
    float    unknown11;
    float    unknown12;
    uint32_t prev_tex_offset;
    uint32_t next_tex_offset;
    uint32_t unknown13;
    uint32_t original_hash;
} WtdTextureMetadata;

typedef struct {
    uint32_t original_resource_type;
    uint32_t original_vft;
    uint32_t original_block_map_ptr;
    uint32_t original_parent_dict;
    uint32_t original_usage_count;
} WtdFileMeta;

#ifdef __cplusplus
extern "C" {
#endif

YtdFile *wtd_load(const wchar_t *path);
bool wtd_save(const YtdFile *wtd, const wchar_t *path);
void wtd_free(YtdFile *wtd);

#ifdef __cplusplus
}
#endif
