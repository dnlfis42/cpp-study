#include "mempool/v01/memory_pool.hpp"

#include <benchmark/benchmark.h>

#include <new>

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <sys/mman.h>

using mempool::v01::MemoryPool;

namespace {
constexpr std::size_t kAllocSize = 64;
constexpr std::size_t kPoolCapacity = 1 << 20; // 1 MiB

// MAP_POPULATE 변형: ctor에서 미리 page fault 처리.
// MemoryPool과 같은 인터페이스만 노출 (벤치 비교용).
class MemoryPoolPopulate {
public:
    explicit MemoryPoolPopulate(std::size_t capacity)
        : buf_{}, capacity_{capacity}, pos_{} {
        auto ptr = mmap(
            nullptr, capacity_, PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0
        );
        if (ptr == MAP_FAILED) {
            throw std::bad_alloc{};
        }
        buf_ = reinterpret_cast<std::byte*>(ptr);
    }
    ~MemoryPoolPopulate() {
        if (buf_) {
            munmap(buf_, capacity_);
        }
    }

    MemoryPoolPopulate(const MemoryPoolPopulate&) = delete;
    MemoryPoolPopulate& operator=(const MemoryPoolPopulate&) = delete;

    std::size_t available() const noexcept { return capacity_ - pos_; }

    void* allocate(std::size_t n) {
        std::size_t aligned_pos = (pos_ + (alignof(std::max_align_t) - 1)) &
                                  ~(alignof(std::max_align_t) - 1);
        if (aligned_pos > capacity_ || n > capacity_ - aligned_pos) {
            throw std::bad_alloc{};
        }
        pos_ = aligned_pos + n;
        return buf_ + aligned_pos;
    }
    void reset() noexcept { pos_ = 0; }

private:
    std::byte* buf_;
    std::size_t capacity_;
    std::size_t pos_;
};
} // namespace

// 단일 allocate 비용. 풀이 충분히 크고, 반복마다 reset 없음.
// 풀이 가득 차면 종료 — capacity / kAllocSize 만큼만 측정 가능하므로 큰 풀
// 사용.
static void BM_Pool_Allocate_Single(benchmark::State& state) {
    MemoryPool pool{kPoolCapacity};

    for (auto _ : state) {
        if (pool.available() < kAllocSize) {
            pool.reset();
        }
        void* p = pool.allocate(kAllocSize);
        benchmark::DoNotOptimize(p);
    }
}
BENCHMARK(BM_Pool_Allocate_Single);

// arena의 sweet spot: N개 할당 후 일괄 reset.
static void BM_Pool_AllocateReset_Batch(benchmark::State& state) {
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
BENCHMARK(BM_Pool_AllocateReset_Batch)->Arg(64)->Arg(1024)->Arg(16384);

// 비교군: malloc/free 페어를 같은 패턴으로.
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

// 명시적 큰 정렬의 오버헤드. (정렬 자체는 마스킹 한 번이라 거의 차이 없어야 함)
static void BM_Pool_Allocate_Aligned64(benchmark::State& state) {
    MemoryPool pool{kPoolCapacity};

    for (auto _ : state) {
        if (pool.available() < kAllocSize + 64) {
            pool.reset();
        }
        void* p = pool.allocate(kAllocSize, 64);
        benchmark::DoNotOptimize(p);
    }
}
BENCHMARK(BM_Pool_Allocate_Aligned64);

// ---- lazy commit 노출: 할당 후 첫 write까지 포함 ----

// (a) bump + 1바이트 write. 새 페이지 진입 시마다 page fault 비용 누적.
static void BM_Pool_BumpWrite1_Single(benchmark::State& state) {
    MemoryPool pool{kPoolCapacity};

    for (auto _ : state) {
        if (pool.available() < kAllocSize) {
            pool.reset(); // ★ reset은 페이지 회수 안 함, 재사용 → 두 번째
                          // 사이클부터는 fault 없음
        }
        auto* p = static_cast<std::byte*>(pool.allocate(kAllocSize));
        *p = std::byte{1};
        benchmark::DoNotOptimize(p);
    }
}
BENCHMARK(BM_Pool_BumpWrite1_Single);

// (b) bump + memset 64바이트. write 자체 비용 비교.
static void BM_Pool_BumpWrite64_Single(benchmark::State& state) {
    MemoryPool pool{kPoolCapacity};

    for (auto _ : state) {
        if (pool.available() < kAllocSize) {
            pool.reset();
        }
        auto* p = static_cast<std::byte*>(pool.allocate(kAllocSize));
        std::memset(p, 0, kAllocSize);
        benchmark::DoNotOptimize(p);
    }
}
BENCHMARK(BM_Pool_BumpWrite64_Single);

// (c) MAP_POPULATE 풀 + 1바이트 write. ctor에서 모든 페이지 미리 fault 처리.
//     (a)와 비교 시 page fault 비용 차이가 드러남.
static void BM_PopPool_BumpWrite1_Single(benchmark::State& state) {
    MemoryPoolPopulate pool{kPoolCapacity};

    for (auto _ : state) {
        if (pool.available() < kAllocSize) {
            pool.reset();
        }
        auto* p = static_cast<std::byte*>(pool.allocate(kAllocSize));
        *p = std::byte{1};
        benchmark::DoNotOptimize(p);
    }
}
BENCHMARK(BM_PopPool_BumpWrite1_Single);

BENCHMARK_MAIN();
