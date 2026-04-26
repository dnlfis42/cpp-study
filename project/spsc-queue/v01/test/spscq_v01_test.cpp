#include "spscq/v01/spsc_queue.hpp"

#include <gtest/gtest.h>

#include <optional>
#include <thread>
#include <vector>

#include <cstddef>

using spscq::v01::SpscQueue;

// ---- 초기 상태 ----
TEST(SpscQueue, InitialStateIsEmpty) {
    SpscQueue<int, 8> q;
    EXPECT_TRUE(q.empty());
    EXPECT_FALSE(q.full());
    EXPECT_EQ(q.size(), 0u);
    EXPECT_EQ(q.capacity(), 8u);
}

// ---- push/pop 기본 ----
TEST(SpscQueue, PushPop) {
    SpscQueue<int, 8> q;
    EXPECT_TRUE(q.push(42));
    EXPECT_EQ(q.size(), 1u);
    auto v = q.pop();
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, 42);
    EXPECT_EQ(q.size(), 0u);
}

// ---- FIFO 순서 ----
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

// ---- 비어있을 때 pop ----
TEST(SpscQueue, PopEmptyReturnsNullopt) {
    SpscQueue<int, 8> q;
    EXPECT_EQ(q.pop(), std::nullopt);
}

// ---- 가득 찼을 때 push (N=8, 최대 저장 7개) ----
TEST(SpscQueue, PushFullReturnsFalse) {
    SpscQueue<int, 8> q;
    for (int i = 0; i < 7; ++i)
        EXPECT_TRUE(q.push(i));
    EXPECT_TRUE(q.full());
    EXPECT_EQ(q.size(), 7u);
    EXPECT_FALSE(q.push(99));
}

// ---- size() 검증: N=8, 모든 head/tail 조합 ----
TEST(SpscQueue, SizeConsistency) {
    SpscQueue<int, 8> q;

    // 순차적으로 push/pop하며 size 확인
    for (int i = 0; i < 7; ++i) {
        q.push(i);
        EXPECT_EQ(q.size(), static_cast<std::size_t>(i + 1));
    }
    for (int i = 6; i >= 0; --i) {
        q.pop();
        EXPECT_EQ(q.size(), static_cast<std::size_t>(i));
    }
}

// ---- wrap-around ----
TEST(SpscQueue, WrapAround) {
    SpscQueue<int, 8> q;
    for (int i = 0; i < 6; ++i)
        q.push(i);
    for (int i = 0; i < 6; ++i)
        q.pop();
    // head, tail 모두 6 (wrap 직전)
    EXPECT_TRUE(q.push(100));
    EXPECT_TRUE(q.push(200));
    EXPECT_EQ(q.size(), 2u);
    EXPECT_EQ(*q.pop(), 100);
    EXPECT_EQ(*q.pop(), 200);
}

// ---- wrap-around 후 size() 정확성 ----
TEST(SpscQueue, SizeAfterWrapAround) {
    SpscQueue<int, 8> q;
    // tail이 wrap-around된 상태에서 size 검증
    for (int i = 0; i < 7; ++i) q.push(i);
    for (int i = 0; i < 7; ++i) q.pop();
    // head == tail == 7, 빈 상태
    EXPECT_EQ(q.size(), 0u);
    EXPECT_TRUE(q.empty());

    q.push(1); q.push(2); q.push(3);
    EXPECT_EQ(q.size(), 3u);
}

// ---- 멀티스레드: producer/consumer ----
TEST(SpscQueue, MultiThread) {
    constexpr int N = 100'000;
    SpscQueue<int, 1024> q;

    std::thread producer{[&] {
        for (int i = 0; i < N; ++i) {
            while (!q.push(i)) {}
        }
    }};

    std::vector<int> results;
    results.reserve(N);
    std::thread consumer{[&] {
        for (int i = 0; i < N; ++i) {
            std::optional<int> v;
            while (!(v = q.pop()).has_value()) {}
            results.push_back(*v);
        }
    }};

    producer.join();
    consumer.join();

    ASSERT_EQ(static_cast<int>(results.size()), N);
    for (int i = 0; i < N; ++i)
        EXPECT_EQ(results[static_cast<std::size_t>(i)], i);
}
