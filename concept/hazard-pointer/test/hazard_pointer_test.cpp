#include "lf_stack.hpp"

#include <atomic>
#include <iostream>
#include <thread>
#include <vector>

#include <cassert>

static void test_single_thread() {
    hp::LfStack<int> stack;

    assert(!stack.pop().has_value());

    stack.push(1);
    stack.push(2);
    stack.push(3);

    assert(stack.pop().value() == 3);
    assert(stack.pop().value() == 2);
    assert(stack.pop().value() == 1);
    assert(!stack.pop().has_value());

    std::cout << "test_single_thread: OK\n";
}

static void test_multi_thread() {
    constexpr int PRODUCERS = 4;
    constexpr int CONSUMERS = 4;
    constexpr int N = 10000;

    hp::LfStack<int, 16> stack;

    std::atomic<int> total_pushed{0};
    std::atomic<int> total_popped{0};

    std::vector<std::thread> threads;

    for (int t = 0; t < PRODUCERS; ++t) {
        threads.emplace_back([&] {
            for (int i = 0; i < N; ++i) {
                stack.push(i);
                total_pushed.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (int t = 0; t < CONSUMERS; ++t) {
        threads.emplace_back([&] {
            int local = 0;
            while (local < N) {
                if (stack.pop().has_value())
                    ++local;
            }
            total_popped.fetch_add(local, std::memory_order_relaxed);
        });
    }

    for (auto& t : threads)
        t.join();

    assert(total_pushed.load() == PRODUCERS * N);
    assert(total_popped.load() == CONSUMERS * N);

    std::cout << "test_multi_thread: OK (" << total_pushed.load() << " pushes, "
              << total_popped.load() << " pops)\n";
}

int main() {
    test_single_thread();
    test_multi_thread();
    return 0;
}
