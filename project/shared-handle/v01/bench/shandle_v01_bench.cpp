#include "shandle/v01/shared_handle.hpp"

#include <benchmark/benchmark.h>

#include <memory>
#include <thread>
#include <utility>

using shandle::v01::make_handle;

namespace {

struct Item {
    char buf[64];
};

} // namespace

static void BM_Copy_SharedPtr(benchmark::State& state) {
    auto src = std::make_shared<Item>();
    for (auto _ : state) {
        auto copy = src;
        benchmark::DoNotOptimize(copy);
    }
}
BENCHMARK(BM_Copy_SharedPtr);

static void BM_Copy_SharedPtr_Atomic(benchmark::State& state) {
    // pthread 생성 -> __libc_single_threaded = 0
    std::thread t([] {});
    t.join();

    auto src = std::make_shared<Item>();
    for (auto _ : state) {
        auto copy = src;
        benchmark::DoNotOptimize(copy);
    }
}
BENCHMARK(BM_Copy_SharedPtr_Atomic);

static void BM_Copy_SharedHandle(benchmark::State& state) {
    auto src = make_handle<Item>();
    for (auto _ : state) {
        auto copy = src;
        benchmark::DoNotOptimize(copy);
    }
}
BENCHMARK(BM_Copy_SharedHandle);

static void BM_Move_SharedPtr(benchmark::State& state) {
    auto h = std::make_shared<Item>();
    for (auto _ : state) {
        auto moved = std::move(h);
        benchmark::DoNotOptimize(moved);
        h = std::move(moved);
    }
}
BENCHMARK(BM_Move_SharedPtr);

static void BM_Move_SharedHandle(benchmark::State& state) {
    auto h = make_handle<Item>();
    for (auto _ : state) {
        auto moved = std::move(h);
        benchmark::DoNotOptimize(moved);
        h = std::move(moved);
    }
}
BENCHMARK(BM_Move_SharedHandle);

static void BM_Make_SharedPtr(benchmark::State& state) {
    for (auto _ : state) {
        auto h = std::make_shared<Item>();
        benchmark::DoNotOptimize(h);
    }
}
BENCHMARK(BM_Make_SharedPtr);

static void BM_Make_SharedHandle(benchmark::State& state) {
    for (auto _ : state) {
        auto h = make_handle<Item>();
        benchmark::DoNotOptimize(h);
    }
}
BENCHMARK(BM_Make_SharedHandle);

BENCHMARK_MAIN();
