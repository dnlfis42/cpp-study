#include "objpool/v04/object_pool.hpp"

#include <benchmark/benchmark.h>

#include <cstddef>

using objpool::v04::ObjectPool;

namespace {
struct Item {
    char buf[64];
};
} // namespace

// v04는 Handle 전용. Raw 경로 없음.
static void BM_Pool_Handle(benchmark::State& state) {
    const std::size_t cap = static_cast<std::size_t>(state.range(0));
    ObjectPool<Item> pool{cap};

    for (auto _ : state) {
        auto h = pool.acquire();
        benchmark::DoNotOptimize(h);
    }
}
BENCHMARK(BM_Pool_Handle)->Arg(64)->Arg(1024)->Arg(16384);

BENCHMARK_MAIN();
