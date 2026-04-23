// memory-pool/bench/mempool_huge_page_bench
//
// 크로스-버전 측정: 일반 4 KiB 페이지 vs MADV_HUGEPAGE vs MAP_HUGETLB.
// 본 풀 클래스 의존 없이 raw mmap만 사용 — backing storage가 hot path에 미치는
// 영향만 측정.
//
// 핵심 워크로드:
//   - sequential write: 첫 touch (page fault 비용)
//   - random read (stride > page): TLB pressure 노출
//
// MAP_HUGETLB는 시스템에 huge page 예약이 없으면 mmap 실패 → 자동 skip.

#include <benchmark/benchmark.h>

#include <algorithm>
#include <random>
#include <vector>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <sys/mman.h>

namespace {

constexpr std::size_t kPoolSize = 64 * 1024 * 1024; // 64 MiB
constexpr std::size_t kPageSize = 4096;

enum class Mode { Normal, Madvise, Hugetlb };

class Pool {
public:
    Pool(std::size_t size, Mode mode) : size_{size}, ptr_{nullptr} {
        int flags = MAP_PRIVATE | MAP_ANONYMOUS;
        if (mode == Mode::Hugetlb) {
            flags |= MAP_HUGETLB;
        }
        void* p = mmap(nullptr, size_, PROT_READ | PROT_WRITE, flags, -1, 0);
        if (p == MAP_FAILED) {
            return;
        }
        if (mode == Mode::Madvise) {
            madvise(p, size_, MADV_HUGEPAGE);
        }
        ptr_ = p;
    }
    ~Pool() {
        if (ptr_) {
            munmap(ptr_, size_);
        }
    }
    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;

    void* data() const noexcept { return ptr_; }
    bool valid() const noexcept { return ptr_ != nullptr; }

private:
    std::size_t size_;
    void* ptr_;
};

std::vector<std::size_t> shuffled_page_indices() {
    std::vector<std::size_t> idx;
    idx.reserve(kPoolSize / kPageSize);
    for (std::size_t i = 0; i < kPoolSize; i += kPageSize) {
        idx.push_back(i);
    }
    std::mt19937 rng{42};
    std::shuffle(idx.begin(), idx.end(), rng);
    return idx;
}

// 페이지마다 1바이트 read. stride > page → 페이지마다 새 TLB entry 필요.
// 64 MiB / 4 KiB = 16384 페이지. TLB entries 보통 64~512 → 100% miss 보장.
void run_random_read(benchmark::State& state, Mode mode) {
    Pool pool{kPoolSize, mode};
    if (!pool.valid()) {
        state.SkipWithError("mmap failed (huge page 예약 없음?)");
        return;
    }
    auto* base = static_cast<volatile char*>(pool.data());

    auto idx = shuffled_page_indices();

    // pre-fault (page fault 비용 분리)
    for (auto i : idx) {
        base[i] = 0;
    }

    for (auto _ : state) {
        std::int64_t sum = 0;
        for (auto i : idx) {
            sum += base[i];
        }
        benchmark::DoNotOptimize(sum);
    }
    state.SetItemsProcessed(
        state.iterations() * static_cast<std::int64_t>(idx.size())
    );
}

// 첫 touch 비용 (lazy commit / page fault). 매 사이클 새 풀 만들고 sequential
// write.
void run_first_touch(benchmark::State& state, Mode mode) {
    for (auto _ : state) {
        Pool pool{kPoolSize, mode};
        if (!pool.valid()) {
            state.SkipWithError("mmap failed");
            return;
        }
        std::memset(pool.data(), 0, kPoolSize);
        benchmark::DoNotOptimize(pool.data());
    }
    state.SetBytesProcessed(
        state.iterations() * static_cast<std::int64_t>(kPoolSize)
    );
}

} // namespace

static void BM_TLBStress_RandomRead_Normal(benchmark::State& state) {
    run_random_read(state, Mode::Normal);
}
BENCHMARK(BM_TLBStress_RandomRead_Normal);

static void BM_TLBStress_RandomRead_Madvise(benchmark::State& state) {
    run_random_read(state, Mode::Madvise);
}
BENCHMARK(BM_TLBStress_RandomRead_Madvise);

static void BM_TLBStress_RandomRead_Hugetlb(benchmark::State& state) {
    run_random_read(state, Mode::Hugetlb);
}
BENCHMARK(BM_TLBStress_RandomRead_Hugetlb);

static void BM_FirstTouch_Sequential_Normal(benchmark::State& state) {
    run_first_touch(state, Mode::Normal);
}
BENCHMARK(BM_FirstTouch_Sequential_Normal);

static void BM_FirstTouch_Sequential_Madvise(benchmark::State& state) {
    run_first_touch(state, Mode::Madvise);
}
BENCHMARK(BM_FirstTouch_Sequential_Madvise);

static void BM_FirstTouch_Sequential_Hugetlb(benchmark::State& state) {
    run_first_touch(state, Mode::Hugetlb);
}
BENCHMARK(BM_FirstTouch_Sequential_Hugetlb);

BENCHMARK_MAIN();
