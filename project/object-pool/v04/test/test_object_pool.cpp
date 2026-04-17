#include "objpool/v04/object_pool.hpp"

#include <gtest/gtest.h>

#include <set>
#include <utility>
#include <vector>

#include <cstddef>

using objpool::v04::ObjectPool;

// ---- 초기 상태 ----
TEST(ObjectPool, InitialState) {
    ObjectPool<int> pool{4};

    EXPECT_EQ(pool.capacity(), 4u);
    EXPECT_EQ(pool.available(), 4u);
    EXPECT_EQ(pool.in_use(), 0u);
}

// ---- acquire (Handle 전용) ----
TEST(ObjectPool, AcquireOne) {
    ObjectPool<int> pool{4};

    auto h = pool.acquire();
    ASSERT_TRUE(h);
    EXPECT_EQ(pool.available(), 3u);
    EXPECT_EQ(pool.in_use(), 1u);
}

TEST(ObjectPool, AcquireAllAvailable) {
    ObjectPool<int> pool{3};

    auto a = pool.acquire();
    auto b = pool.acquire();
    auto c = pool.acquire();

    EXPECT_TRUE(a);
    EXPECT_TRUE(b);
    EXPECT_TRUE(c);
    EXPECT_EQ(pool.available(), 0u);
    EXPECT_EQ(pool.in_use(), 3u);
}

TEST(ObjectPool, AcquireBeyondCapacityReturnsEmpty) {
    ObjectPool<int> pool{2};

    auto a = pool.acquire();
    auto b = pool.acquire();
    auto c = pool.acquire();

    EXPECT_TRUE(a);
    EXPECT_TRUE(b);
    EXPECT_FALSE(c);
    EXPECT_EQ(pool.in_use(), 2u);
}

// ---- 자동 반환 ----
TEST(ObjectPool, AutoReleaseOnScope) {
    ObjectPool<int> pool{2};
    {
        auto h = pool.acquire();
        EXPECT_EQ(pool.in_use(), 1u);
    }
    EXPECT_EQ(pool.in_use(), 0u);
    EXPECT_EQ(pool.available(), 2u);
}

TEST(ObjectPool, ReleaseAllAndReacquire) {
    ObjectPool<int> pool{3};
    {
        auto a = pool.acquire();
        auto b = pool.acquire();
        auto c = pool.acquire();
    }
    EXPECT_EQ(pool.in_use(), 0u);

    EXPECT_TRUE(pool.acquire());
    EXPECT_TRUE(pool.acquire());
    EXPECT_TRUE(pool.acquire());
}

// ---- LIFO 순서 ----
TEST(ObjectPool, LifoOrder) {
    ObjectPool<int> pool{3};

    int* a_addr = nullptr;
    int* b_addr = nullptr;
    {
        auto a = pool.acquire();
        auto b = pool.acquire();
        a_addr = a.get();
        b_addr = b.get();
    } // 스코프 끝: b 먼저 release (역순 소멸), 그 다음 a

    auto x = pool.acquire();
    auto y = pool.acquire();

    // 마지막에 release된 a가 먼저 나옴 (LIFO)
    EXPECT_EQ(x.get(), a_addr);
    EXPECT_EQ(y.get(), b_addr);
}

// ---- 포인터 유일성 ----
TEST(ObjectPool, AcquiredPointersAreUnique) {
    constexpr std::size_t N = 8;
    ObjectPool<int> pool{N};
    std::set<int*> seen;
    std::vector<ObjectPool<int>::Handle> holders;

    for (std::size_t i = 0; i < N; ++i) {
        auto h = pool.acquire();
        ASSERT_TRUE(h);
        auto [_, inserted] = seen.insert(h.get());
        EXPECT_TRUE(inserted) << "duplicate pointer at i=" << i;
        holders.push_back(std::move(h));
    }
}

// ---- 객체 사용 ----
TEST(ObjectPool, AcquiredObjectIsWritable) {
    ObjectPool<int> pool{2};

    int* addr = nullptr;
    {
        auto h = pool.acquire();
        *h = 42;
        addr = h.get();
        EXPECT_EQ(*h, 42);
    }

    auto h2 = pool.acquire();
    EXPECT_EQ(h2.get(), addr);
    EXPECT_EQ(*h2, 42);
}

TEST(ObjectPool, WorksWithNonPodType) {
    struct Item {
        int x{0};
        std::vector<int> data;
    };

    ObjectPool<Item> pool{2};

    auto h = pool.acquire();
    ASSERT_TRUE(h);
    h->x = 7;
    h->data.push_back(100);
}

// ---- Handle move / reset ----
TEST(ObjectPool, HandleMove) {
    ObjectPool<int> pool{2};

    auto h1 = pool.acquire();
    ASSERT_TRUE(h1);
    EXPECT_EQ(pool.in_use(), 1u);

    auto h2 = std::move(h1);
    EXPECT_FALSE(h1);
    EXPECT_TRUE(h2);
    EXPECT_EQ(pool.in_use(), 1u);
}

TEST(ObjectPool, HandleReset) {
    ObjectPool<int> pool{2};

    auto h = pool.acquire();
    EXPECT_EQ(pool.in_use(), 1u);

    h.reset();
    EXPECT_EQ(pool.in_use(), 0u);
}

// ---- Pool move ----
TEST(ObjectPool, PoolMoveConstruct) {
    ObjectPool<int> a{3};
    auto h1 = a.acquire();
    EXPECT_EQ(a.in_use(), 1u);

    // Handle outstanding일 때 move는 위험 — 테스트에선 raw로 분리
    (void)h1.release();

    ObjectPool<int> b{std::move(a)};
    EXPECT_EQ(b.in_use(), 1u);
    EXPECT_EQ(b.capacity(), 3u);
}

// ---- 경계 ----
TEST(ObjectPool, CapacityOneExhaustAndRestore) {
    ObjectPool<int> pool{1};

    int* addr = nullptr;
    {
        auto a = pool.acquire();
        ASSERT_TRUE(a);
        EXPECT_FALSE(pool.acquire());
        addr = a.get();
    }

    auto b = pool.acquire();
    ASSERT_TRUE(b);
    EXPECT_EQ(b.get(), addr);
}
