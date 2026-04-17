#include "objpool/v05/object_pool.hpp"

#include <benchmark/benchmark.h>

#include <cstddef>

using objpool::v05::ObjectPool;

namespace {
struct Item {
    char buf[64];
};
} // namespace

// hot loop — 청크 1개로 충분, 성장 비용 없음 (고정 풀과 비교 가능)
static void BM_Pool_Handle(benchmark::State& state) {
    const std::size_t chunk_size = static_cast<std::size_t>(state.range(0));
    ObjectPool<Item> pool{chunk_size};

    for (auto _ : state) {
        auto h = pool.acquire();
        benchmark::DoNotOptimize(h);
    }
}
BENCHMARK(BM_Pool_Handle)->Arg(64)->Arg(1024)->Arg(16384);

BENCHMARK_MAIN();
