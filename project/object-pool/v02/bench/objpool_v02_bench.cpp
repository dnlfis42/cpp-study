#include "objpool/v02/object_pool.hpp"

#include <benchmark/benchmark.h>

using objpool::v02::ObjectPool;

namespace {
struct Item {
    char buf[64];
};
} // namespace

static void BM_Raw(benchmark::State& state) {
    ObjectPool<Item> pool{64};

    for (auto _ : state) {
        Item* p = pool.acquire();
        benchmark::DoNotOptimize(p);
        pool.release(p);
    }
}
BENCHMARK(BM_Raw);

static void BM_Handle(benchmark::State& state) {
    ObjectPool<Item> pool{64};

    for (auto _ : state) {
        auto h = pool.acquire_unique();
        benchmark::DoNotOptimize(h);
    }
}
BENCHMARK(BM_Handle);

BENCHMARK_MAIN();
