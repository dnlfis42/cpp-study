#include "objpool/v05/object_pool.hpp"
#include "objpool/v06/object_pool.hpp"

#include <benchmark/benchmark.h>

#include <cstddef>

namespace {
// trivial T — ctor/dtor = no-op. 순수 메모리 관리 비용만 측정.
struct Item {
    char buf[64];
};
} // namespace

// v05: Node[] 기반 free list, pre-construct
static void BM_v05_Acquire(benchmark::State& state) {
    const std::size_t chunk_size = static_cast<std::size_t>(state.range(0));
    objpool::v05::ObjectPool<Item> pool{chunk_size};

    for (auto _ : state) {
        auto h = pool.acquire();
        benchmark::DoNotOptimize(h);
    }
}
BENCHMARK(BM_v05_Acquire)->Arg(64)->Arg(1024)->Arg(16384);

// v06: MemoryPool v03 slab 기반, placement new + explicit dtor
static void BM_v06_Acquire(benchmark::State& state) {
    objpool::v06::ObjectPool<Item> pool;

    for (auto _ : state) {
        auto h = pool.acquire();
        benchmark::DoNotOptimize(h);
    }
}
BENCHMARK(BM_v06_Acquire);

BENCHMARK_MAIN();
