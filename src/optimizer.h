#ifndef EO_OPTIMIZER_H
#define EO_OPTIMIZER_H

#include "types.h"

typedef struct {
    int ytd_index;
    int tex_index;
} DupEntry;

typedef struct {
    char hash_key[65];
    DupEntry *entries;
    int count;
} DupGroup;

/* Find duplicate textures across all loaded YTDs by name or data hash */
DupGroup *optimizer_find_duplicates(YtdFile **ytds, int ytd_count, int *out_group_count, bool by_hash);

void optimizer_free_groups(DupGroup *groups, int count);

/* Smart optimize: resize textures above a threshold */
int optimizer_smart_resize(YtdFile *ytd, int max_width, int max_height, TexFormat target_fmt, int max_mips);

#endif
