#ifndef EO_PARALLEL_H
#define EO_PARALLEL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Number of usable hardware threads (cached, always >= 1). */
int eo_hw_threads(void);

/* "Inner-serial" hint: when set, leaf operations that would otherwise spawn
 * their own threads (BCn block decode, single-texture encode) run serially.
 * Used so an outer parallel-for over whole textures doesn't oversubscribe by
 * nesting more threads inside each item. Set on the main thread around a
 * parallel region; workers only read it. */
void eo_set_inner_serial(int on);
int  eo_inner_serial(void);

/* Split [begin, end) into contiguous chunks and run `fn(start, end, ctx)` for
 * each chunk on its own worker thread. Blocks until all complete.
 *   max_threads <= 0  -> use eo_hw_threads()
 * Use this when the per-item cost is uniform (decode rows, pixel conversion). */
typedef void (*eo_range_fn)(size_t start, size_t end, void *ctx);
void eo_parallel_range(size_t begin, size_t end, eo_range_fn fn, void *ctx,
                       int max_threads);

/* Dynamic work queue: `fn(index, ctx)` is pulled by `max_threads` workers via an
 * atomic counter, so uneven item costs (whole textures) stay balanced.
 * Blocks until every index in [begin, end) has been processed. */
typedef void (*eo_index_fn)(size_t index, void *ctx);
void eo_parallel_for(size_t begin, size_t end, eo_index_fn fn, void *ctx,
                     int max_threads);

#ifdef __cplusplus
}
#endif

#endif
