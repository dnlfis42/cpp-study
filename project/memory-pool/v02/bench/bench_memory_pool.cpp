#include "mempool/v02/memory_pool.hpp"

#include <benchmark/benchmark.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

using mempool::v02::MemoryPool;

namespace {
constexpr std::size_t kAllocSize = 64;
constexpr std::size_t kChunkSize = 1 << 20; // 1 MiB

// large 분류를 강제할 청크 크기. threshold = 64 KiB.
constexpr std::size_t kSmallChunkSize = 128 * 1024;
constexpr std::size_t kLargeAllocSize = 96 * 1024; // > 64 KiB → large
} // namespace

// ---- small path: v01과 동일 패턴 ----

// 단일 small allocate. v01 1.34 ns 와 비교.
static void BM_PoolV02_Allocate_Single(benchmark::State& state) {
    MemoryPool pool{kChunkSize};

    for (auto _ : state) {
        if (pool.total_in_use() + kAllocSize > kChunkSize) {
            pool.reset();
        }
        void* p = pool.allocate(kAllocSize);
        benchmark::DoNotOptimize(p);
    }
}
BENCHMARK(BM_PoolV02_Allocate_Single);

// small batch + reset. 청크 1개에 다 들어가는 크기.
static void BM_PoolV02_AllocateReset_Batch(benchmark::State& state) {
    const std::size_t batch = static_cast<std::size_t>(state.range(0));
    MemoryPool pool{batch * kAllocSize + 4096};

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
BENCHMARK(BM_PoolV02_AllocateReset_Batch)->Arg(64)->Arg(1024)->Arg(16384);

// 비교군: malloc/free 페어.
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
        state.iterations() * static_cast<std::int64_t>(batch)
    );
}
BENCHMARK(BM_Malloc_Free_Batch)->Arg(64)->Arg(1024)->Arg(16384);

// ---- v02 고유: chunk 자동 성장 ----

// 청크가 작아 batch 처리 중 여러 번 새 청크 생성.
// 첫 사이클은 mmap 비용 누적, 이후 사이클은 reset 후 청크 재사용.
// 정상 상태에서는 reset 후 small 보존이라 mmap 0회 → small bump 비용만 남음.
static void BM_PoolV02_AllocateReset_GrowsChunks(benchmark::State& state) {
    // chunk_size = 4096, 64B씩 → 청크당 64개.
    const std::size_t batch = static_cast<std::size_t>(state.range(0));
    MemoryPool pool{4096};

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
BENCHMARK(BM_PoolV02_AllocateReset_GrowsChunks)->Arg(64)->Arg(1024)->Arg(16384);

// ---- v02 고유: large path ----

// large allocate + reset. 매 호출마다 mmap, reset마다 munmap.
static void BM_PoolV02_LargeAllocateReset(benchmark::State& state) {
    MemoryPool pool{kSmallChunkSize};

    for (auto _ : state) {
        void* p = pool.allocate(kLargeAllocSize);
        benchmark::DoNotOptimize(p);
        pool.reset();
    }
}
BENCHMARK(BM_PoolV02_LargeAllocateReset);

// large 비교군: malloc 같은 크기. (glibc는 ≥M_MMAP_THRESHOLD에서 mmap 사용)
static void BM_Malloc_Large(benchmark::State& state) {
    for (auto _ : state) {
        void* p = std::malloc(kLargeAllocSize);
        benchmark::DoNotOptimize(p);
        std::free(p);
    }
}
BENCHMARK(BM_Malloc_Large);

// ---- v02 고유: mixed ----

// small N개 + large 1개 + reset. 실전 패턴 (큰 버퍼 1개 + 작은 객체 다수).
static void BM_PoolV02_MixedReset(benchmark::State& state) {
    const std::size_t small_count = static_cast<std::size_t>(state.range(0));
    MemoryPool pool{kSmallChunkSize};

    for (auto _ : state) {
        for (std::size_t i = 0; i < small_count; ++i) {
            void* p = pool.allocate(kAllocSize);
            benchmark::DoNotOptimize(p);
        }
        void* lp = pool.allocate(kLargeAllocSize);
        benchmark::DoNotOptimize(lp);
        pool.reset();
    }
    state.SetItemsProcessed(
        state.iterations() * static_cast<std::int64_t>(small_count + 1)
    );
}
BENCHMARK(BM_PoolV02_MixedReset)->Arg(64)->Arg(1024);

BENCHMARK_MAIN();
