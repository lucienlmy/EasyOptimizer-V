#ifndef EO_OPTIMIZER_H
#define EO_OPTIMIZER_H

#include "types.h"

typedef struct {
    int ytd_index;
    int tex_index;
} DupEntry;

typedef struct {
    char hash_key[97];   /* up to "name|sha256" */
    DupEntry *entries;
    int count;
} DupGroup;

typedef enum {
    DUP_BY_NAME = 0,
    DUP_BY_HASH = 1,
    DUP_BY_NAME_AND_HASH = 2,
} DupCriterion;

typedef enum {
    MIGRATE_MIXED       = 0,   /* keep master in original + copy to consolidated; drop dups from other YTDs */
    MIGRATE_REMOVE_DUPS = 1,   /* move every distinct-name instance to consolidated; clear originals */
} MigrateStrategy;

/* Migration limit (16 MiB per consolidated YTD before splitting) */
#define MIGRATE_GREEN_LIMIT (16ULL * 1024ULL * 1024ULL)

/* A texture slated for removal from an original YTD on migration commit.
 * Stored by pointer so it survives list re-sorting between detect and migrate. */
typedef struct {
    YtdFile *ytd;
    int tex_index;
} PendingRemoval;

/* Find duplicate textures across all loaded YTDs */
DupGroup *optimizer_find_duplicates(YtdFile **ytds, int ytd_count, int *out_group_count, DupCriterion criterion);

void optimizer_free_groups(DupGroup *groups, int count);

/* Build the consolidation PREVIEW: create consolidated_textures_*.ytd files (marked
 * ->is_preview = true) and append them to *io_ytds. Does NOT modify the originals;
 * instead it allocates *out_removals describing which textures would be removed on
 * commit. Caller frees *out_removals with free(). Returns consolidated YTDs created. */
int optimizer_build_consolidation(YtdFile **io_ytds, int *io_ytd_count, int max_ytds,
                                  DupCriterion criterion, MigrateStrategy strategy,
                                  PendingRemoval **out_removals, int *out_removal_count,
                                  int *out_dup_groups, int *out_textures, int *out_consolidated);

/* Commit a migration: remove the listed textures from their originals. */
void optimizer_apply_removals(PendingRemoval *removals, int count);

/* Smart optimize: resize textures above a threshold */
int optimizer_smart_resize(YtdFile *ytd, int max_width, int max_height, TexFormat target_fmt, int max_mips);

#endif
