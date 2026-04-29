#include "objpool/v04/object_pool.hpp"

#include <benchmark/benchmark.h>

using objpool::v04::ObjectPool;

namespace {

struct Item {
    char buf[64];
};

} // namespace

static void BM_ObjPool_Handle(benchmark::State& state) {
    ObjectPool<Item> pool{64};

    for (auto _ : state) {
        auto h = pool.acquire();
        benchmark::DoNotOptimize(h);
    }
}
BENCHMARK(BM_ObjPool_Handle);

BENCHMARK_MAIN();
