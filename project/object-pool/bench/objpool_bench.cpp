#include "objpool/v01/object_pool.hpp"
#include "objpool/v02/object_pool.hpp"
#include "objpool/v03/object_pool.hpp"
#include "objpool/v04/object_pool.hpp"
#include "objpool/v05/object_pool.hpp"

#include <benchmark/benchmark.h>

namespace {
struct Item {
    char buf[64];
};
} // namespace

// v01 - Raw
static void BM_v01_Raw(benchmark::State& state) {
    objpool::v01::ObjectPool<Item> pool{64};
    for (auto _ : state) {
        Item* p = pool.acquire();
        benchmark::DoNotOptimize(p);
        pool.release(p);
    }
}
BENCHMARK(BM_v01_Raw);

// v02 - Raw
static void BM_v02_Raw(benchmark::State& state) {
    objpool::v02::ObjectPool<Item> pool{64};
    for (auto _ : state) {
        Item* p = pool.acquire();
        benchmark::DoNotOptimize(p);
        pool.release(p);
    }
}
BENCHMARK(BM_v02_Raw);

// v02 - Handle (sizeof(Deleter) = 8, sizeof(Handle) = 16)
static void BM_v02_Handle(benchmark::State& state) {
    objpool::v02::ObjectPool<Item> pool{64};
    for (auto _ : state) {
        auto h = pool.acquire_unique();
        benchmark::DoNotOptimize(h);
    }
}
BENCHMARK(BM_v02_Handle);

// v03 - Handle (sizeof(Deleter) = 16, sizeof(Handle) = 24)
static void BM_v03_Handle(benchmark::State& state) {
    objpool::v03::ObjectPool<Item> pool{64};
    for (auto _ : state) {
        auto h = pool.acquire();
        benchmark::DoNotOptimize(h);
    }
}
BENCHMARK(BM_v03_Handle);

// v04 - Handle (growable, chunk Node[], sizeof(Handle) = 24)
static void BM_v04_Handle(benchmark::State& state) {
    objpool::v04::ObjectPool<Item> pool{64};
    for (auto _ : state) {
        auto h = pool.acquire();
        benchmark::DoNotOptimize(h);
    }
}
BENCHMARK(BM_v04_Handle);

// v05 - Handle (mempool backend, placement new, sizeof(Handle) = 16)
static void BM_v05_Handle(benchmark::State& state) {
    objpool::v05::ObjectPool<Item> pool;
    for (auto _ : state) {
        auto h = pool.acquire();
        benchmark::DoNotOptimize(h);
    }
}
BENCHMARK(BM_v05_Handle);

BENCHMARK_MAIN();
