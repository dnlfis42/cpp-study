#include "objpool/v01/object_pool.hpp"

#include <benchmark/benchmark.h>

using objpool::v01::ObjectPool;

namespace {
struct Item {
    char buf[64];
};
} // namespace

static void BM_Acquire_Raw(benchmark::State& state) {
    ObjectPool<Item> pool{64};

    for (auto _ : state) {
        Item* p = pool.acquire();
        benchmark::DoNotOptimize(p);
        pool.release(p);
    }
}
BENCHMARK(BM_Acquire_Raw);

static void BM_Acquire_NewDelete(benchmark::State& state) {
    for (auto _ : state) {
        Item* p = new Item;
        benchmark::DoNotOptimize(p);
        delete p;
    }
}
BENCHMARK(BM_Acquire_NewDelete);

BENCHMARK_MAIN();
