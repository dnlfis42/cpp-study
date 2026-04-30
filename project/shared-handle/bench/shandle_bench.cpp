#include "shandle/v01/shared_handle.hpp"
#include "shandle/v02/shared_handle.hpp"

#include <benchmark/benchmark.h>

#include <memory>
#include <thread>
#include <utility>

namespace {

struct ItemV01 {
    char buf[64];
};

struct ItemV02 : shandle::v02::IntrusiveBase {
    char buf[64];
};

} // namespace

// ---------------------------------------------------------------------------
// Copy
// ---------------------------------------------------------------------------

static void BM_Copy_SharedPtr(benchmark::State& state) {
    auto src = std::make_shared<ItemV01>();
    for (auto _ : state) {
        auto copy = src;
        benchmark::DoNotOptimize(copy);
    }
}
BENCHMARK(BM_Copy_SharedPtr);

static void BM_Copy_V01(benchmark::State& state) {
    auto src = shandle::v01::make_handle<ItemV01>();
    for (auto _ : state) {
        auto copy = src;
        benchmark::DoNotOptimize(copy);
    }
}
BENCHMARK(BM_Copy_V01);

static void BM_Copy_V02(benchmark::State& state) {
    auto src = shandle::v02::make_handle<ItemV02>();
    for (auto _ : state) {
        auto copy = src;
        benchmark::DoNotOptimize(copy);
    }
}
BENCHMARK(BM_Copy_V02);

// ---------------------------------------------------------------------------
// Move
// ---------------------------------------------------------------------------

static void BM_Move_SharedPtr(benchmark::State& state) {
    auto h = std::make_shared<ItemV01>();
    for (auto _ : state) {
        auto moved = std::move(h);
        benchmark::DoNotOptimize(moved);
        h = std::move(moved);
    }
}
BENCHMARK(BM_Move_SharedPtr);

static void BM_Move_V01(benchmark::State& state) {
    auto h = shandle::v01::make_handle<ItemV01>();
    for (auto _ : state) {
        auto moved = std::move(h);
        benchmark::DoNotOptimize(moved);
        h = std::move(moved);
    }
}
BENCHMARK(BM_Move_V01);

static void BM_Move_V02(benchmark::State& state) {
    auto h = shandle::v02::make_handle<ItemV02>();
    for (auto _ : state) {
        auto moved = std::move(h);
        benchmark::DoNotOptimize(moved);
        h = std::move(moved);
    }
}
BENCHMARK(BM_Move_V02);

// ---------------------------------------------------------------------------
// Make
// ---------------------------------------------------------------------------

static void BM_Make_SharedPtr(benchmark::State& state) {
    for (auto _ : state) {
        auto h = std::make_shared<ItemV01>();
        benchmark::DoNotOptimize(h);
    }
}
BENCHMARK(BM_Make_SharedPtr);

static void BM_Make_V01(benchmark::State& state) {
    for (auto _ : state) {
        auto h = shandle::v01::make_handle<ItemV01>();
        benchmark::DoNotOptimize(h);
    }
}
BENCHMARK(BM_Make_V01);

static void BM_Make_V02(benchmark::State& state) {
    for (auto _ : state) {
        auto h = shandle::v02::make_handle<ItemV02>();
        benchmark::DoNotOptimize(h);
    }
}
BENCHMARK(BM_Make_V02);

static void BM_Copy_SharedPtr_Atomic(benchmark::State& state) {
    std::thread t([] {});
    t.join();

    auto src = std::make_shared<ItemV01>();
    for (auto _ : state) {
        auto copy = src;
        benchmark::DoNotOptimize(copy);
    }
}
BENCHMARK(BM_Copy_SharedPtr_Atomic);

BENCHMARK_MAIN();
