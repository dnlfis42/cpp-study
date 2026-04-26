#include "spscq/v03/spsc_queue.hpp"

#include <benchmark/benchmark.h>

#include <atomic>
#include <thread>

using spscq::v03::SpscQueue;

template <std::size_t N>
static void BM_Spsc_Throughput(benchmark::State& state) {
    SpscQueue<int, N> q;

    std::atomic<bool> running{true};
    std::thread producer{[&] {
        int i = 0;
        while (running.load(std::memory_order_relaxed)) {
            q.push(i++);
        }
    }};

    for (auto _ : state) {
        std::optional<int> v;
        while (!(v = q.pop()).has_value()) {
        }
        benchmark::DoNotOptimize(v);
    }

    running.store(false, std::memory_order_relaxed);
    producer.join();
}

BENCHMARK_TEMPLATE(BM_Spsc_Throughput, 1024);
BENCHMARK_TEMPLATE(BM_Spsc_Throughput, 4096);
BENCHMARK_TEMPLATE(BM_Spsc_Throughput, 16384);

BENCHMARK_MAIN();
