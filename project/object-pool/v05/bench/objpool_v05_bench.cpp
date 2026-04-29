#include "objpool/v05/object_pool.hpp"

#include <benchmark/benchmark.h>

namespace {
struct Item {
    char buf[64];
};
} // namespace

static void BM_Handle(benchmark::State& state) {
    objpool::v05::ObjectPool<Item> pool;

    for (auto _ : state) {
        auto h = pool.acquire();
        benchmark::DoNotOptimize(h);
    }
}
BENCHMARK(BM_Handle);

BENCHMARK_MAIN();
