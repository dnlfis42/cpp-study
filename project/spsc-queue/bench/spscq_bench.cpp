#include "spscq/v02/spsc_queue.hpp"
#include "spscq/v03/spsc_queue.hpp"
#include "spscq/v04/spsc_queue.hpp"
#include "spscq/v05/spsc_queue.hpp"

#include <benchmark/benchmark.h>

#include <atomic>
#include <cstddef>
#include <optional>
#include <thread>

struct alignas(64) Msg {
    std::byte data[64];
};

template <template <typename, std::size_t> class Q, std::size_t N>
static void BM_Spsc_Burst(benchmark::State& state) {
    Q<Msg, N> q;

    constexpr std::size_t cap = N - 1;
    constexpr std::size_t high = cap * 75 / 100;
    constexpr std::size_t low = cap * 25 / 100;

    std::atomic<bool> running{true};
    std::thread producer{[&] {
        Msg m{};
        while (running.load(std::memory_order_relaxed)) {
            while (q.size() < high) {
                q.push(m);
            }
            while (q.size() > low && running.load(std::memory_order_relaxed)) {
            }
        }
    }};

    for (auto _ : state) {
        std::optional<Msg> v;
        while (!(v = q.pop()).has_value()) {
        }
        benchmark::DoNotOptimize(v);
    }

    running.store(false, std::memory_order_relaxed);
    producer.join();
}

// v02
BENCHMARK_TEMPLATE(BM_Spsc_Burst, spscq::v02::SpscQueue, 1024);
BENCHMARK_TEMPLATE(BM_Spsc_Burst, spscq::v02::SpscQueue, 4096);
BENCHMARK_TEMPLATE(BM_Spsc_Burst, spscq::v02::SpscQueue, 16384);

// v03
BENCHMARK_TEMPLATE(BM_Spsc_Burst, spscq::v03::SpscQueue, 1024);
BENCHMARK_TEMPLATE(BM_Spsc_Burst, spscq::v03::SpscQueue, 4096);
BENCHMARK_TEMPLATE(BM_Spsc_Burst, spscq::v03::SpscQueue, 16384);

// v04
BENCHMARK_TEMPLATE(BM_Spsc_Burst, spscq::v04::SpscQueue, 1024);
BENCHMARK_TEMPLATE(BM_Spsc_Burst, spscq::v04::SpscQueue, 4096);
BENCHMARK_TEMPLATE(BM_Spsc_Burst, spscq::v04::SpscQueue, 16384);

// v05
BENCHMARK_TEMPLATE(BM_Spsc_Burst, spscq::v05::SpscQueue, 1024);
BENCHMARK_TEMPLATE(BM_Spsc_Burst, spscq::v05::SpscQueue, 4096);
BENCHMARK_TEMPLATE(BM_Spsc_Burst, spscq::v05::SpscQueue, 16384);

BENCHMARK_MAIN();
