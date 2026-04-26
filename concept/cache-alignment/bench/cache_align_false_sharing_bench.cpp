// false sharing 측정. 비용을 layer별로 분해.
//
// 시나리오:
//   [1] single thread, 같은 변수 반복 update (베이스라인)
//   [2] 2 thread, 같은 cache line의 다른 변수, 다른 물리 코어
//   [3] 2 thread, 정렬로 다른 cache line, 다른 물리 코어
//   [4] 2 thread, 같은 atomic 공유 (LOCK + contention)
//   [5] 2 thread, aligned atomic (LOCK only, contention 없음)
//   [6] 2 thread, false sharing on HT sibling (MOMC 노출)
//   [7] 2 thread, aligned on HT sibling (HT 자체 비용만)
//
// 코어 핀:
//   - kCoreA(2) + kCoreB(4): 다른 물리 코어
//   - kCoreA(2) + kCoreA_HT(8): 같은 물리 코어의 HT sibling

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <cmath>
#include <cstdint>

#include <pthread.h>
#include <sched.h>

namespace {

constexpr std::uint64_t kIters = 100'000'000;
constexpr int kRepetitions = 7;
constexpr int kCoreA = 2;
constexpr int kCoreB = 4;    // 다른 물리 코어
constexpr int kCoreA_HT = 8; // logical 8 = physical core 2의 HT sibling

void pin_to_core(int core) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(static_cast<std::size_t>(core), &set);
    pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
}

struct Packed {
    std::uint64_t a;
    std::uint64_t b;
};

struct Aligned {
    alignas(64) std::uint64_t a;
    alignas(64) std::uint64_t b;
};

// 매 iteration의 메모리 접근을 강제 (컴파일러가 루프를 fold하지 못하게).
// "+m"는 read+write 메모리 operand → load + store 강제.
#define FORCE_MEMORY(x) asm volatile("" : "+m"(x))

struct AtomicShared {
    std::atomic<std::uint64_t> v;
};

struct AtomicAligned {
    alignas(64) std::atomic<std::uint64_t> a;
    alignas(64) std::atomic<std::uint64_t> b;
};

using clock_t_ = std::chrono::steady_clock;
using ns_t = std::chrono::nanoseconds;

// 두 스레드를 barrier로 동기화 후 동시에 N번 update. 측정은 release ~ join.
template <typename FA, typename FB>
double run_two_threads_ns_on(int core_a, int core_b, FA body_a, FB body_b) {
    std::atomic<int> ready{0};
    std::atomic<bool> go{false};

    std::thread t1{[&, core_a] {
        pin_to_core(core_a);
        ready.fetch_add(1, std::memory_order_relaxed);
        while (!go.load(std::memory_order_acquire)) {
        }
        body_a();
    }};
    std::thread t2{[&, core_b] {
        pin_to_core(core_b);
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

template <typename FA, typename FB>
double run_two_threads_ns(FA body_a, FB body_b) {
    return run_two_threads_ns_on(kCoreA, kCoreB, body_a, body_b);
}

double run_single_thread_ns(std::uint64_t* p) {
    pin_to_core(kCoreA);
    auto start = clock_t_::now();
    for (std::uint64_t i = 0; i < kIters; ++i) {
        ++(*p);
        FORCE_MEMORY(*p);
    }
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

void report(const char* name, std::vector<double> samples_ns) {
    auto s = stats_of(samples_ns);
    double total_ops =
        static_cast<double>(kIters) * 2.0; // 2 thread (single thread는 ×0.5)
    if (std::string(name).find("Single") != std::string::npos) {
        total_ops = static_cast<double>(kIters);
    }
    double median_ns_per_op = s.median / total_ops;
    double mops_per_sec = total_ops / s.median * 1e3; // ns→s, M scale
    std::cout << name << "\n"
              << "  median: " << s.median / 1e6 << " ms"
              << "  (per op: " << median_ns_per_op << " ns"
              << ", " << mops_per_sec << " Mops/s)\n"
              << "  min/max: " << s.min / 1e6 << " / " << s.max / 1e6 << " ms"
              << "  CV: " << s.cv << "%\n\n";
}

} // namespace

int main(int argc, char** argv) {
    // argv[1]: 실행할 시나리오 번호 (1~7). 없으면 전체.
    // perf stat 측정용 단일 시나리오 실행 지원.
    int only = (argc > 1) ? std::atoi(argv[1]) : 0;
    auto enabled = [&](int n) { return only == 0 || only == n; };

    std::cout << "=== cache_align_false_sharing_bench ===\n"
              << "iters per thread: " << kIters << "\n"
              << "repetitions: " << kRepetitions << "\n"
              << "cores: " << kCoreA << ", " << kCoreB
              << " (HT sibling: " << kCoreA_HT << ")\n";
    if (only)
        std::cout << "running only scenario [" << only << "]\n";
    std::cout << "\n";

    // 1. Single thread baseline.
    if (enabled(1)) {
        std::vector<double> samples;
        for (int i = 0; i < kRepetitions; ++i) {
            std::uint64_t v = 0;
            samples.push_back(run_single_thread_ns(&v));
        }
        report("[1] Single thread, same variable", samples);
    }

    // 2. False sharing: 두 스레드가 같은 cache line의 다른 변수.
    if (enabled(2)) {
        std::vector<double> samples;
        for (int i = 0; i < kRepetitions; ++i) {
            Packed shared{};
            auto body_a = [&] {
                for (std::uint64_t k = 0; k < kIters; ++k) {
                    ++shared.a;
                    FORCE_MEMORY(shared.a);
                }
            };
            auto body_b = [&] {
                for (std::uint64_t k = 0; k < kIters; ++k) {
                    ++shared.b;
                    FORCE_MEMORY(shared.b);
                }
            };
            samples.push_back(run_two_threads_ns(body_a, body_b));
        }
        report("[2] 2 thread, false sharing (same cache line)", samples);
    }

    // 3. Aligned: 두 스레드가 다른 cache line.
    if (enabled(3)) {
        std::vector<double> samples;
        for (int i = 0; i < kRepetitions; ++i) {
            Aligned shared{};
            auto body_a = [&] {
                for (std::uint64_t k = 0; k < kIters; ++k) {
                    ++shared.a;
                    FORCE_MEMORY(shared.a);
                }
            };
            auto body_b = [&] {
                for (std::uint64_t k = 0; k < kIters; ++k) {
                    ++shared.b;
                    FORCE_MEMORY(shared.b);
                }
            };
            samples.push_back(run_two_threads_ns(body_a, body_b));
        }
        report("[3] 2 thread, aligned (separate cache lines)", samples);
    }

    // 4. 진짜 공유 atomic — LOCK + contention 합산.
    if (enabled(4)) {
        std::vector<double> samples;
        for (int i = 0; i < kRepetitions; ++i) {
            AtomicShared shared{};
            auto body_a = [&] {
                for (std::uint64_t k = 0; k < kIters; ++k) {
                    shared.v.fetch_add(1, std::memory_order_relaxed);
                }
            };
            auto body_b = body_a;
            samples.push_back(run_two_threads_ns(body_a, body_b));
        }
        report("[4] 2 thread, shared atomic (LOCK + contention)", samples);
    }

    // 5. Aligned atomic — LOCK prefix 단독 (contention 없음).
    if (enabled(5))
    //    [3] vs [5]: 순수 LOCK 오버헤드
    //    [5] vs [4]: 순수 contention 비용
    {
        std::vector<double> samples;
        for (int i = 0; i < kRepetitions; ++i) {
            AtomicAligned shared{};
            auto body_a = [&] {
                for (std::uint64_t k = 0; k < kIters; ++k) {
                    shared.a.fetch_add(1, std::memory_order_relaxed);
                }
            };
            auto body_b = [&] {
                for (std::uint64_t k = 0; k < kIters; ++k) {
                    shared.b.fetch_add(1, std::memory_order_relaxed);
                }
            };
            samples.push_back(run_two_threads_ns(body_a, body_b));
        }
        report("[5] 2 thread, aligned atomic (LOCK only)", samples);
    }

    // 6. False sharing on HT sibling — L1 공유, MESI 충돌 없음.
    if (enabled(6))
    //    [2]와 같은 시나리오지만 같은 물리 코어의 두 HT logical core 사용.
    {
        std::vector<double> samples;
        for (int i = 0; i < kRepetitions; ++i) {
            Packed shared{};
            auto body_a = [&] {
                for (std::uint64_t k = 0; k < kIters; ++k) {
                    ++shared.a;
                    FORCE_MEMORY(shared.a);
                }
            };
            auto body_b = [&] {
                for (std::uint64_t k = 0; k < kIters; ++k) {
                    ++shared.b;
                    FORCE_MEMORY(shared.b);
                }
            };
            samples.push_back(
                run_two_threads_ns_on(kCoreA, kCoreA_HT, body_a, body_b)
            );
        }
        report(
            "[6] 2 thread, false sharing on HT sibling (shared L1)", samples
        );
    }

    // 7. Aligned on HT sibling — false sharing 없이 HT 자체 비용만.
    if (enabled(7))
    //    [6] - [7] = HT 위에서의 순수 false sharing 비용
    //    [7] - [3] = HT 자체 비용 (execution 자원 공유)
    {
        std::vector<double> samples;
        for (int i = 0; i < kRepetitions; ++i) {
            Aligned shared{};
            auto body_a = [&] {
                for (std::uint64_t k = 0; k < kIters; ++k) {
                    ++shared.a;
                    FORCE_MEMORY(shared.a);
                }
            };
            auto body_b = [&] {
                for (std::uint64_t k = 0; k < kIters; ++k) {
                    ++shared.b;
                    FORCE_MEMORY(shared.b);
                }
            };
            samples.push_back(
                run_two_threads_ns_on(kCoreA, kCoreA_HT, body_a, body_b)
            );
        }
        report("[7] 2 thread, aligned on HT sibling (HT cost only)", samples);
    }

    return 0;
}
