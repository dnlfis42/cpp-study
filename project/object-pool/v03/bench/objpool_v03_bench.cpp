#include "objpool/v03/object_pool.hpp"

#include <benchmark/benchmark.h>

#include <cstddef>

using objpool::v03::ObjectPool;

namespace {
struct Item {
    char buf[64];
};
} // namespace

// Raw acquire/release — v01 경로 (비교 기준)
static void BM_Pool_Raw(benchmark::State& state) {
    const std::size_t cap = static_cast<std::size_t>(state.range(0));
    ObjectPool<Item> pool{cap};

    for (auto _ : state) {
        Item* p = pool.acquire();
        benchmark::DoNotOptimize(p);
        pool.release(p);
    }
}
BENCHMARK(BM_Pool_Raw)->Arg(64)->Arg(1024)->Arg(16384);

// RAII Handle — Deleter 호출 오버헤드 측정
static void BM_Pool_Handle(benchmark::State& state) {
    const std::size_t cap = static_cast<std::size_t>(state.range(0));
    ObjectPool<Item> pool{cap};

    for (auto _ : state) {
        auto h = pool.acquire_unique();
        benchmark::DoNotOptimize(h);
    }
}
BENCHMARK(BM_Pool_Handle)->Arg(64)->Arg(1024)->Arg(16384);

BENCHMARK_MAIN();
