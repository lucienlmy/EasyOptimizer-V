#include "eo_parallel.h"

#include <atomic>
#include <thread>
#include <vector>

extern "C" int eo_hw_threads(void) {
    static int cached = 0;
    if (!cached) {
        int n = (int)std::thread::hardware_concurrency();
        cached = n > 0 ? n : 1;
    }
    return cached;
}

static std::atomic<int> g_inner_serial{0};
extern "C" void eo_set_inner_serial(int on) { g_inner_serial.store(on ? 1 : 0, std::memory_order_relaxed); }
extern "C" int  eo_inner_serial(void) { return g_inner_serial.load(std::memory_order_relaxed); }

extern "C" void eo_parallel_range(size_t begin, size_t end, eo_range_fn fn,
                                  void *ctx, int max_threads) {
    if (!fn || end <= begin) return;
    const size_t total = end - begin;

    int nt = max_threads > 0 ? max_threads : eo_hw_threads();
    if (nt < 1) nt = 1;
    if ((size_t)nt > total) nt = (int)total;

    if (nt <= 1) { fn(begin, end, ctx); return; }

    std::vector<std::thread> threads;
    threads.reserve(nt);
    const size_t chunk = total / (size_t)nt;
    const size_t rem   = total % (size_t)nt;
    size_t s = begin;
    for (int t = 0; t < nt; ++t) {
        size_t c = chunk + ((size_t)t < rem ? 1u : 0u);
        size_t e = s + c;
        threads.emplace_back(fn, s, e, ctx);
        s = e;
    }
    for (auto &th : threads) th.join();
}

extern "C" void eo_parallel_for(size_t begin, size_t end, eo_index_fn fn,
                                void *ctx, int max_threads) {
    if (!fn || end <= begin) return;
    const size_t total = end - begin;

    int nt = max_threads > 0 ? max_threads : eo_hw_threads();
    if (nt < 1) nt = 1;
    if ((size_t)nt > total) nt = (int)total;

    if (nt <= 1) {
        for (size_t i = begin; i < end; ++i) fn(i, ctx);
        return;
    }

    std::atomic<size_t> next(begin);
    auto worker = [&]() {
        for (;;) {
            size_t i = next.fetch_add(1, std::memory_order_relaxed);
            if (i >= end) break;
            fn(i, ctx);
        }
    };
    std::vector<std::thread> threads;
    threads.reserve(nt);
    for (int t = 0; t < nt; ++t) threads.emplace_back(worker);
    for (auto &th : threads) th.join();
}
