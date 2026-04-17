#include "objpool/v01/object_pool.hpp"

#include <benchmark/benchmark.h>

#include <random>
#include <vector>

#include <cstddef>

using objpool::v01::ObjectPool;

namespace {

// 벤치 대상 타입 — 캐시 라인 크기
struct Item {
    char buf[64];
};

// free_list를 섞어 "프로그램 중간 상태"를 재현하는 warmup.
// 고정 시드로 재현성 확보. 종료 시 모든 슬롯 반환 → available == capacity.
template <typename T>
void warmup_pool(ObjectPool<T>& pool, std::size_t churn_factor = 10) {
    const std::size_t cap = pool.capacity();
    std::mt19937 gen(42);
    std::vector<T*> held;
    held.reserve(cap);

    for (std::size_t i = 0; i < cap * churn_factor; ++i) {
        const bool do_acquire =
            held.size() < cap / 2 || (gen() % 2 == 0 && held.size() < cap);
        if (do_acquire) {
            if (T* p = pool.acquire()) {
                held.push_back(p);
            }
        } else if (!held.empty()) {
            const std::size_t idx = gen() % held.size();
            pool.release(held[idx]);
            held[idx] = held.back();
            held.pop_back();
        }
    }
    for (T* p : held) {
        pool.release(p);
    }
}

} // namespace

// --- 이상 조건: 초기화 직후 (free_list 정렬 + cold storage) ---
static void BM_Pool_NoWarmup(benchmark::State& state) {
    const std::size_t cap = static_cast<std::size_t>(state.range(0));
    ObjectPool<Item> pool{cap};

    for (auto _ : state) {
        Item* p = pool.acquire();
        benchmark::DoNotOptimize(p);
        pool.release(p);
    }
}
BENCHMARK(BM_Pool_NoWarmup)->Arg(64)->Arg(1024)->Arg(16384);

// --- 현실 조건: warmup 후 (free_list 섞임 + warm storage) ---
static void BM_Pool_Warmup(benchmark::State& state) {
    const std::size_t cap = static_cast<std::size_t>(state.range(0));
    ObjectPool<Item> pool{cap};
    warmup_pool(pool);

    for (auto _ : state) {
        Item* p = pool.acquire();
        benchmark::DoNotOptimize(p);
        pool.release(p);
    }
}
BENCHMARK(BM_Pool_Warmup)->Arg(64)->Arg(1024)->Arg(16384);

// --- 베이스라인: new/delete ---
static void BM_NewDelete(benchmark::State& state) {
    for (auto _ : state) {
        Item* p = new Item;
        benchmark::DoNotOptimize(p);
        delete p;
    }
}
BENCHMARK(BM_NewDelete);

BENCHMARK_MAIN();
