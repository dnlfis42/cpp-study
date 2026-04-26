#include "spscq/v05/spsc_queue.hpp"

#include <gtest/gtest.h>

#include <optional>
#include <thread>
#include <vector>

#include <cstddef>

using spscq::v05::SpscQueue;

TEST(SpscQueue, InitialStateIsEmpty) {
    SpscQueue<int, 8> q;
    EXPECT_TRUE(q.empty());
    EXPECT_FALSE(q.full());
    EXPECT_EQ(q.size(), 0u);
    EXPECT_EQ(q.capacity(), 7u);
}

TEST(SpscQueue, PushPop) {
    SpscQueue<int, 8> q;
    EXPECT_TRUE(q.push(42));
    EXPECT_EQ(q.size(), 1u);
    auto v = q.pop();
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, 42);
    EXPECT_EQ(q.size(), 0u);
}

TEST(SpscQueue, FifoOrder) {
    SpscQueue<int, 8> q;
    for (int i = 0; i < 5; ++i)
        EXPECT_TRUE(q.push(i));
    for (int i = 0; i < 5; ++i) {
        auto v = q.pop();
        ASSERT_TRUE(v.has_value());
        EXPECT_EQ(*v, i);
    }
}

TEST(SpscQueue, PopEmptyReturnsNullopt) {
    SpscQueue<int, 8> q;
    EXPECT_EQ(q.pop(), std::nullopt);
}

TEST(SpscQueue, PushFullReturnsFalse) {
    SpscQueue<int, 8> q;
    for (int i = 0; i < 7; ++i)
        EXPECT_TRUE(q.push(i));
    EXPECT_TRUE(q.full());
    EXPECT_EQ(q.size(), 7u);
    EXPECT_FALSE(q.push(99));
}

TEST(SpscQueue, WrapAround) {
    SpscQueue<int, 8> q;
    for (int i = 0; i < 6; ++i)
        q.push(i);
    for (int i = 0; i < 6; ++i)
        q.pop();
    EXPECT_TRUE(q.push(100));
    EXPECT_TRUE(q.push(200));
    EXPECT_EQ(q.size(), 2u);
    EXPECT_EQ(*q.pop(), 100);
    EXPECT_EQ(*q.pop(), 200);
}

TEST(SpscQueue, MultiThread) {
    constexpr int N = 100'000;
    SpscQueue<int, 1024> q;

    std::thread producer{[&] {
        for (int i = 0; i < N; ++i) {
            while (!q.push(i)) {
            }
        }
    }};

    std::vector<int> results;
    results.reserve(N);
    std::thread consumer{[&] {
        for (int i = 0; i < N; ++i) {
            std::optional<int> v;
            while (!(v = q.pop()).has_value()) {
            }
            results.push_back(*v);
        }
    }};

    producer.join();
    consumer.join();

    ASSERT_EQ(static_cast<int>(results.size()), N);
    for (int i = 0; i < N; ++i)
        EXPECT_EQ(results[static_cast<std::size_t>(i)], i);
}
