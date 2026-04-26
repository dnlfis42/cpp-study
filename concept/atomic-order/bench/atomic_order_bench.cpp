// memory_order별 비용 측정.
//
// 시나리오:
//   [1] fetch_add relaxed,  단일 스레드
//   [2] fetch_add acq_rel,  단일 스레드
//   [3] fetch_add seq_cst,  단일 스레드
//   [4] store(release) + load(acquire), 단일 스레드
//   [5] store(seq_cst) + load(seq_cst), 단일 스레드
//   [6] store(relaxed) + load(relaxed), 단일 스레드
//   [7] fetch_add relaxed,  멀티 스레드 (다른 물리 코어, 공유 변수)
//   [8] fetch_add seq_cst,  멀티 스레드 (다른 물리 코어, 공유 변수)

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

#include <cmath>
#include <cstdint>
#include <cstdlib>

#include <pthread.h>
#include <sched.h>

namespace {

constexpr std::uint64_t kIters = 100'000'000;
constexpr int kRepetitions = 7;
constexpr int kCoreA = 2;
constexpr int kCoreB = 4;

void pin_to_core(int core) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(static_cast<std::size_t>(core), &set);
    pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
}

using clock_t_ = std::chrono::steady_clock;
using ns_t = std::chrono::nanoseconds;

template <typename FA, typename FB>
double run_two_threads_ns(FA body_a, FB body_b) {
    std::atomic<int> ready{0};
    std::atomic<bool> go{false};

    std::thread t1{[&] {
        pin_to_core(kCoreA);
        ready.fetch_add(1, std::memory_order_relaxed);
        while (!go.load(std::memory_order_acquire)) {
        }
        body_a();
    }};
    std::thread t2{[&] {
        pin_to_core(kCoreB);
        ready.fetch_add(1, std::memory_order_relaxed);
        while (!go.load(std::memory_order_acquire)) {
        }
        body_b();
    }};
    while (ready.load(std::memory_order_relaxed) < 2) {
    }

    auto start = clock_t_::now();
    go.store(true, std::memory_order_release);
    t1.join();
    t2.join();
    auto end = clock_t_::now();

    return static_cast<double>(
        std::chrono::duration_cast<ns_t>(end - start).count()
    );
}

struct Stats {
    double min;
    double median;
    double max;
    double cv;
};

Stats stats_of(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    double mn = v.front();
    double mx = v.back();
    double med = v[v.size() / 2];
    double sum = 0;
    for (auto x : v)
        sum += x;
    double mean = sum / static_cast<double>(v.size());
    double var = 0;
    for (auto x : v)
        var += (x - mean) * (x - mean);
    double sd = (v.size() > 1)
                    ? std::sqrt(var / static_cast<double>(v.size() - 1))
                    : 0.0;
    double cv = (mean > 0) ? sd / mean * 100.0 : 0.0;
    return {mn, med, mx, cv};
}

void report(const char* name, std::vector<double> samples_ns, bool two_thread) {
    auto s = stats_of(samples_ns);
    double total_ops = static_cast<double>(kIters) * (two_thread ? 2.0 : 1.0);
    double median_ns_per_op = s.median / total_ops;
    double mops = total_ops / s.median * 1e3;
    std::cout << name << "\n"
              << "  median: " << s.median / 1e6 << " ms"
              << "  (per op: " << median_ns_per_op << " ns"
              << ", " << mops << " Mops/s)\n"
              << "  min/max: " << s.min / 1e6 << " / " << s.max / 1e6 << " ms"
              << "  CV: " << s.cv << "%\n\n";
}

} // namespace

int main(int argc, char** argv) {
    int only = (argc > 1) ? std::atoi(argv[1]) : 0;
    auto enabled = [&](int n) { return only == 0 || only == n; };

    std::cout << "=== atomic_order_bench ===\n"
              << "iters per thread: " << kIters << "\n"
              << "repetitions: " << kRepetitions << "\n"
              << "cores: " << kCoreA << ", " << kCoreB << "\n";
    if (only)
        std::cout << "running only scenario [" << only << "]\n";
    std::cout << "\n";

    // 1. fetch_add relaxed, 단일
    if (enabled(1)) {
        std::vector<double> samples;
        for (int i = 0; i < kRepetitions; ++i) {
            std::atomic<std::uint64_t> v{0};
            pin_to_core(kCoreA);
            auto start = clock_t_::now();
            for (std::uint64_t k = 0; k < kIters; ++k)
                v.fetch_add(1, std::memory_order_relaxed);
            auto end = clock_t_::now();
            samples.push_back(
                static_cast<double>(
                    std::chrono::duration_cast<ns_t>(end - start).count()
                )
            );
        }
        report("[1] fetch_add relaxed, single", samples, false);
    }

    // 2. fetch_add acq_rel, 단일
    if (enabled(2)) {
        std::vector<double> samples;
        for (int i = 0; i < kRepetitions; ++i) {
            std::atomic<std::uint64_t> v{0};
            pin_to_core(kCoreA);
            auto start = clock_t_::now();
            for (std::uint64_t k = 0; k < kIters; ++k)
                v.fetch_add(1, std::memory_order_acq_rel);
            auto end = clock_t_::now();
            samples.push_back(
                static_cast<double>(
                    std::chrono::duration_cast<ns_t>(end - start).count()
                )
            );
        }
        report("[2] fetch_add acq_rel, single", samples, false);
    }

    // 3. fetch_add seq_cst, 단일
    if (enabled(3)) {
        std::vector<double> samples;
        for (int i = 0; i < kRepetitions; ++i) {
            std::atomic<std::uint64_t> v{0};
            pin_to_core(kCoreA);
            auto start = clock_t_::now();
            for (std::uint64_t k = 0; k < kIters; ++k)
                v.fetch_add(1, std::memory_order_seq_cst);
            auto end = clock_t_::now();
            samples.push_back(
                static_cast<double>(
                    std::chrono::duration_cast<ns_t>(end - start).count()
                )
            );
        }
        report("[3] fetch_add seq_cst, single", samples, false);
    }

    // 4. store(release) + load(acquire), 단일
    if (enabled(4)) {
        std::vector<double> samples;
        for (int i = 0; i < kRepetitions; ++i) {
            std::atomic<std::uint64_t> v{0};
            pin_to_core(kCoreA);
            auto start = clock_t_::now();
            for (std::uint64_t k = 0; k < kIters; ++k) {
                v.store(k, std::memory_order_release);
                auto r = v.load(std::memory_order_acquire);
                (void)r;
            }
            auto end = clock_t_::now();
            samples.push_back(
                static_cast<double>(
                    std::chrono::duration_cast<ns_t>(end - start).count()
                )
            );
        }
        report("[4] store(release)+load(acquire), single", samples, false);
    }

    // 5. store(seq_cst) + load(seq_cst), 단일
    if (enabled(5)) {
        std::vector<double> samples;
        for (int i = 0; i < kRepetitions; ++i) {
            std::atomic<std::uint64_t> v{0};
            pin_to_core(kCoreA);
            auto start = clock_t_::now();
            for (std::uint64_t k = 0; k < kIters; ++k) {
                v.store(k, std::memory_order_seq_cst);
                auto r = v.load(std::memory_order_seq_cst);
                (void)r;
            }
            auto end = clock_t_::now();
            samples.push_back(
                static_cast<double>(
                    std::chrono::duration_cast<ns_t>(end - start).count()
                )
            );
        }
        report("[5] store(seq_cst)+load(seq_cst), single", samples, false);
    }

    // 6. store(relaxed) + load(relaxed), 단일
    if (enabled(6)) {
        std::vector<double> samples;
        for (int i = 0; i < kRepetitions; ++i) {
            std::atomic<std::uint64_t> v{0};
            pin_to_core(kCoreA);
            auto start = clock_t_::now();
            for (std::uint64_t k = 0; k < kIters; ++k) {
                v.store(k, std::memory_order_relaxed);
                auto r = v.load(std::memory_order_relaxed);
                (void)r;
            }
            auto end = clock_t_::now();
            samples.push_back(
                static_cast<double>(
                    std::chrono::duration_cast<ns_t>(end - start).count()
                )
            );
        }
        report("[6] store(relaxed)+load(relaxed), single", samples, false);
    }

    // 7. fetch_add relaxed, 멀티 (공유 변수)
    if (enabled(7)) {
        std::vector<double> samples;
        for (int i = 0; i < kRepetitions; ++i) {
            std::atomic<std::uint64_t> v{0};
            auto body = [&] {
                for (std::uint64_t k = 0; k < kIters; ++k)
                    v.fetch_add(1, std::memory_order_relaxed);
            };
            samples.push_back(run_two_threads_ns(body, body));
        }
        report("[7] fetch_add relaxed, multi (shared)", samples, true);
    }

    // 8. fetch_add seq_cst, 멀티 (공유 변수)
    if (enabled(8)) {
        std::vector<double> samples;
        for (int i = 0; i < kRepetitions; ++i) {
            std::atomic<std::uint64_t> v{0};
            auto body = [&] {
                for (std::uint64_t k = 0; k < kIters; ++k)
                    v.fetch_add(1, std::memory_order_seq_cst);
            };
            samples.push_back(run_two_threads_ns(body, body));
        }
        report("[8] fetch_add seq_cst, multi (shared)", samples, true);
    }

    return 0;
}
