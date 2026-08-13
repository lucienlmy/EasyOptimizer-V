/* ============================================================================
 *  rsc7_pages.h — RSC7 page allocation (port of CodeWalker AssignPositions2)
 * ----------------------------------------------------------------------------
 *  A resource segment is not one flat buffer: the game allocates a *set of
 *  pages* and the page sizes are encoded in the 32-bit sys/gfx flags word. Two
 *  hard rules come out of that:
 *
 *    1. A block must never straddle a page boundary — pages are separate
 *       allocations at unrelated addresses, so a split block loads as garbage
 *       ("address is neither virtual nor physical").
 *    2. The declared page total is what the engine actually reserves. Rounding
 *       the whole segment up to one power-of-two page satisfies rule 1, but
 *       makes a 2.3 MiB dictionary reserve 16 MiB of streaming memory.
 *
 *  CodeWalker satisfies both by bin-packing blocks into several pages of
 *  descending size, so the tail is covered at `baseSize` granularity instead of
 *  being rounded to the next power of two.
 *
 *  Flags layout (RpfResourcePageFlags, CodeWalker/RpfFile.cs:2702):
 *      BaseShift = value & 0xF          BaseSize = 0x200 << BaseShift
 *      counts/sizes, largest page first:
 *          (v>>4)  & 0x01  ->  BaseSize << 8      (this file's index 4)
 *          (v>>5)  & 0x03  ->  BaseSize << 7      (index 3)
 *          (v>>7)  & 0x0F  ->  BaseSize << 6      (index 2)
 *          (v>>11) & 0x3F  ->  BaseSize << 5      (index 1)
 *          (v>>17) & 0x7F  ->  BaseSize << 4      (index 0)
 *      The four single-count tail buckets (bits 24..27) are decoded by the game
 *      but CodeWalker's writer does not emit them, so neither do we.
 *
 *  Index 0 is BaseSize<<4 == 0x2000 << BaseShift, which is why the packer's own
 *  `base_size` starts at 0x2000 and the emitted BaseShift is shared with it.
 * ==========================================================================*/
#ifndef RSC7_PAGES_H
#define RSC7_PAGES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define RSC7_PAGE_ALIGN      16   /* CodeWalker ALIGN_SIZE */
#define RSC7_NUM_PAGE_SIZES  5
#define RSC7_MAX_PAGES       128  /* engine-wide chunk limit, shared sys+gfx */

/* Max page count per size index, i.e. the width of each bit field. */
static const uint32_t rsc7_page_count_max[RSC7_NUM_PAGE_SIZES] = { 0x7F, 0x3F, 0xF, 0x3, 0x1 };

typedef struct {
    int    size_index;  /* 0..4 */
    int    page_index;  /* which page of that size */
    size_t offset;      /* offset inside the page */
} Rsc7Slot;

/* Pack `count` blocks into RSC7 pages.
 *
 *   block_sizes    [in]  size of each block, in caller order
 *   count          [in]  number of blocks (0 is valid)
 *   max_page_count [in]  page budget (128 total across both segments)
 *   out_offsets    [out] segment-relative offset of each block, caller order
 *   out_flags      [out] page flags, WITHOUT the version nibble in bits 28-31
 *   out_total      [out] total bytes spanned by the allocated pages
 *
 * The largest block always lands at offset 0, which is what the system segment
 * needs (its root structure must sit exactly at the segment base).
 *
 * Returns false only if the blocks cannot be packed at all (a single block
 * larger than the biggest representable page, or more pages than the budget). */
static inline bool rsc7_pack(const size_t *block_sizes, int count,
                             uint32_t max_page_count,
                             size_t *out_offsets, uint32_t *out_flags,
                             size_t *out_total)
{
    *out_flags = 0;
    *out_total = 0;
    if (count <= 0) return true;

    size_t max_block = 0, min_block = (size_t)-1;
    for (int i = 0; i < count; i++) {
        if (block_sizes[i] > max_block) max_block = block_sizes[i];
        if (block_sizes[i] < min_block) min_block = block_sizes[i];
    }
    if (max_block == 0) {
        /* Every block is empty. Offsets still have to be defined: the caller
         * writes them into resource pointers regardless of block size, and
         * leaving the array untouched would emit uninitialised addresses. */
        for (int i = 0; i < count; i++) out_offsets[i] = 0;
        return true;
    }

    /* Order blocks largest-first: best-fit-decreasing wastes far less tail space
     * than insertion order, and it puts the largest block in the first page. */
    int *order = (int *)malloc((size_t)count * sizeof(int));
    Rsc7Slot *slots = (Rsc7Slot *)malloc((size_t)count * sizeof(Rsc7Slot));
    if (!order || !slots) { free(order); free(slots); return false; }
    for (int i = 0; i < count; i++) order[i] = i;
    for (int i = 1; i < count; i++) {          /* insertion sort, descending */
        int k = order[i];
        size_t kv = block_sizes[k];
        int j = i - 1;
        while (j >= 0 && block_sizes[order[j]] < kv) { order[j+1] = order[j]; j--; }
        order[j+1] = k;
    }

    /* baseSize must be at least the smallest block (so tiny blocks don't force a
     * page smaller than themselves) and, times 16, at least the largest block
     * (so the largest block fits the biggest page size we can express). */
    int    base_shift = 0;
    size_t base_size  = 0x2000;
    while ((base_size < min_block || base_size * 16 < max_block) && base_shift < 0xF) {
        base_shift++;
        base_size = (size_t)0x2000 << base_shift;
    }
    if (base_size * 16 < max_block) { free(order); free(slots); return false; }

    uint32_t page_counts[RSC7_NUM_PAGE_SIZES];
    /* fill[s][p] = bytes used in page p of size index s. Bounded by the field
     * widths above, so 127 is the largest count we ever need to track. */
    static const int kMaxPagesPerSize = 0x7F;
    size_t *fill = (size_t *)malloc(RSC7_NUM_PAGE_SIZES * (kMaxPagesPerSize + 1) * sizeof(size_t));
    if (!fill) { free(order); free(slots); return false; }

    for (;;) {
        memset(page_counts, 0, sizeof(page_counts));

        /* Smallest page size index that can hold the largest block. */
        int    largest_i  = 0;
        size_t largest_sz = base_size;
        while (largest_sz < max_block && largest_i < RSC7_NUM_PAGE_SIZES - 1) {
            largest_i++;
            largest_sz *= 2;
        }

        bool overflow = false;
        for (int n = 0; n < count && !overflow; n++) {
            int    bi   = order[n];
            size_t size = block_sizes[bi];

            if (n == 0) {   /* largest block opens the first page */
                fill[largest_i * (kMaxPagesPerSize + 1) + 0] = size;
                page_counts[largest_i] = 1;
                slots[bi].size_index = largest_i;
                slots[bi].page_index = 0;
                slots[bi].offset     = 0;
                continue;
            }

            /* Smallest page size this block fits in. */
            int    si = 0;
            size_t ps = base_size;
            while (size > ps && si < largest_i) { si++; ps *= 2; }

            /* First-fit into an already-open page of that size or bigger. */
            bool placed = false;
            for (int t = si; t <= largest_i && !placed; t++) {
                size_t tps = base_size << t;
                for (uint32_t p = 0; p < page_counts[t]; p++) {
                    size_t *used = &fill[t * (kMaxPagesPerSize + 1) + p];
                    size_t at = (*used + (RSC7_PAGE_ALIGN - 1)) & ~(size_t)(RSC7_PAGE_ALIGN - 1);
                    if (at + size <= tps) {
                        *used = at + size;
                        slots[bi].size_index = t;
                        slots[bi].page_index = (int)p;
                        slots[bi].offset     = at;
                        placed = true;
                        break;
                    }
                }
            }
            if (placed) continue;

            /* Otherwise open a new page at the block's own size class. */
            if (page_counts[si] > (uint32_t)kMaxPagesPerSize) { overflow = true; break; }
            uint32_t p = page_counts[si];
            fill[si * (kMaxPagesPerSize + 1) + p] = size;
            page_counts[si] = p + 1;
            slots[bi].size_index = si;
            slots[bi].page_index = (int)p;
            slots[bi].offset     = 0;
        }

        uint32_t total_pages = 0;
        if (!overflow) {
            for (int i = 0; i < RSC7_NUM_PAGE_SIZES; i++) {
                if (page_counts[i] > rsc7_page_count_max[i]) { overflow = true; break; }
                total_pages += page_counts[i];
            }
            if (total_pages > max_page_count) overflow = true;
        }
        if (!overflow) break;

        /* Too many pages to encode — double the base page size and retry, which
         * halves the page count at the cost of coarser granularity. */
        if (base_shift >= 0xF) { free(order); free(slots); free(fill); return false; }
        base_shift++;
        base_size = (size_t)0x2000 << base_shift;
    }

    /* Pages are laid out largest size first, matching RpfResourcePageFlags.Pages. */
    size_t page_base[RSC7_NUM_PAGE_SIZES];
    size_t cursor = 0;
    for (int i = RSC7_NUM_PAGE_SIZES - 1; i >= 0; i--) {
        page_base[i] = cursor;
        cursor += (base_size << i) * page_counts[i];
    }

    for (int i = 0; i < count; i++) {
        if (block_sizes[i] == 0) { out_offsets[i] = 0; continue; }
        int s = slots[i].size_index;
        out_offsets[i] = page_base[s]
                       + (base_size << s) * (size_t)slots[i].page_index
                       + slots[i].offset;
    }

    uint32_t v = (uint32_t)base_shift & 0xF;
    v |= (page_counts[4] & 0x01u) << 4;
    v |= (page_counts[3] & 0x03u) << 5;
    v |= (page_counts[2] & 0x0Fu) << 7;
    v |= (page_counts[1] & 0x3Fu) << 11;
    v |= (page_counts[0] & 0x7Fu) << 17;

    *out_flags = v;
    *out_total = cursor;

    free(order);
    free(slots);
    free(fill);
    return true;
}

/* Number of pages described by a flags word (version nibble ignored). */
static inline uint32_t rsc7_page_count(uint32_t flags) {
    return ((flags >> 4)  & 0x01) + ((flags >> 5)  & 0x03) + ((flags >> 7)  & 0x0F)
         + ((flags >> 11) & 0x3F) + ((flags >> 17) & 0x7F) + ((flags >> 24) & 0x01)
         + ((flags >> 25) & 0x01) + ((flags >> 26) & 0x01) + ((flags >> 27) & 0x01);
}

#endif /* RSC7_PAGES_H */
