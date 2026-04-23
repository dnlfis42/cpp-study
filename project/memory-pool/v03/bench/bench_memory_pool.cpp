#include "mempool/v03/memory_pool.hpp"

#include <benchmark/benchmark.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

using mempool::v03::MemoryPool;

namespace {
constexpr std::size_t kAllocSize = 64;
} // namespace

// ---- slab의 sweet spot: alloc/dealloc pair ----

// 같은 슬롯이 LIFO로 계속 재활용. 진짜 hot path.
static void BM_PoolV03_AllocDealloc_Pair(benchmark::State& state) {
    const std::size_t n = static_cast<std::size_t>(state.range(0));
    MemoryPool pool;

    for (auto _ : state) {
        void* p = pool.allocate(n);
        benchmark::DoNotOptimize(p);
        pool.deallocate(p, n);
    }
}
BENCHMARK(BM_PoolV03_AllocDealloc_Pair)->Arg(16)->Arg(64)->Arg(1024);

// 비교군: malloc/free pair. tcache hit 경로.
static void BM_Malloc_Free_Pair(benchmark::State& state) {
    const std::size_t n = static_cast<std::size_t>(state.range(0));

    for (auto _ : state) {
        void* p = std::malloc(n);
        benchmark::DoNotOptimize(p);
        std::free(p);
    }
}
BENCHMARK(BM_Malloc_Free_Pair)->Arg(16)->Arg(64)->Arg(1024);

// ---- alloc batch → dealloc batch (LIFO 깊이 효과) ----

// N개 alloc 후 N개 dealloc. free list 길이가 0~N 사이를 오감.
// pair 패턴과 달리 매번 다른 슬롯을 alloc/dealloc.
static void BM_PoolV03_AllocBatch_DeallocBatch(benchmark::State& state) {
    const std::size_t batch = static_cast<std::size_t>(state.range(0));
    MemoryPool pool;
    std::vector<void*> ptrs(batch);

    for (auto _ : state) {
        for (std::size_t i = 0; i < batch; ++i) {
            ptrs[i] = pool.allocate(kAllocSize);
            benchmark::DoNotOptimize(ptrs[i]);
        }
        for (std::size_t i = 0; i < batch; ++i) {
            pool.deallocate(ptrs[i], kAllocSize);
        }
    }
    state.SetItemsProcessed(
        state.iterations() * 2 * static_cast<std::int64_t>(batch)
    );
}
BENCHMARK(BM_PoolV03_AllocBatch_DeallocBatch)->Arg(64)->Arg(1024)->Arg(16384);

static void BM_Malloc_Free_Batch(benchmark::State& state) {
    const std::size_t batch = static_cast<std::size_t>(state.range(0));
    std::vector<void*> ptrs(batch);

    for (auto _ : state) {
        for (std::size_t i = 0; i < batch; ++i) {
            ptrs[i] = std::malloc(kAllocSize);
            benchmark::DoNotOptimize(ptrs[i]);
        }
        for (std::size_t i = 0; i < batch; ++i) {
            std::free(ptrs[i]);
        }
    }
    state.SetItemsProcessed(
        state.iterations() * 2 * static_cast<std::int64_t>(batch)
    );
}
BENCHMARK(BM_Malloc_Free_Batch)->Arg(64)->Arg(1024)->Arg(16384);

// ---- arena 비교군: alloc batch + reset (slab은 매번 munmap이라 불리) ----

// v01/v02의 sweet spot 패턴을 slab으로 측정. 매 사이클 slab munmap+mmap이라
// slab엔 최악의 시나리오. 학습 비교용.
static void BM_PoolV03_AllocateReset_Batch(benchmark::State& state) {
    const std::size_t batch = static_cast<std::size_t>(state.range(0));
    MemoryPool pool;

    for (auto _ : state) {
        for (std::size_t i = 0; i < batch; ++i) {
            void* p = pool.allocate(kAllocSize);
            benchmark::DoNotOptimize(p);
        }
        pool.reset();
    }
    state.SetItemsProcessed(
        state.iterations() * static_cast<std::int64_t>(batch)
    );
}
BENCHMARK(BM_PoolV03_AllocateReset_Batch)->Arg(64)->Arg(1024)->Arg(16384);

// ---- size class별 cost (분기/인덱싱이 size에 무관한지) ----

// 새 slab 트리거 없이 한 번만 alloc/dealloc — pair와 같지만 명시.
// pair 결과로 충분.

BENCHMARK_MAIN();
