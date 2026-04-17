#include "objpool/v01/object_pool.hpp"

#include <gtest/gtest.h>

#include <set>
#include <utility>
#include <vector>

#include <cstddef>

using objpool::v01::ObjectPool;

// ---- 초기 상태 ----
TEST(ObjectPool, InitialState) {
    ObjectPool<int> pool{4};

    EXPECT_EQ(pool.capacity(), 4u);
    EXPECT_EQ(pool.available(), 4u);
    EXPECT_EQ(pool.in_use(), 0u);
}

// ---- acquire 기본 ----
TEST(ObjectPool, AcquireOne) {
    ObjectPool<int> pool{4};

    int* p = pool.acquire();

    ASSERT_NE(p, nullptr);
    EXPECT_EQ(pool.available(), 3u);
    EXPECT_EQ(pool.in_use(), 1u);
}

TEST(ObjectPool, AcquireAllAvailable) {
    ObjectPool<int> pool{3};

    int* a = pool.acquire();
    int* b = pool.acquire();
    int* c = pool.acquire();

    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(pool.available(), 0u);
    EXPECT_EQ(pool.in_use(), 3u);
}

TEST(ObjectPool, AcquireBeyondCapacityReturnsNull) {
    ObjectPool<int> pool{2};

    int* a = pool.acquire();
    int* b = pool.acquire();
    int* c = pool.acquire(); // 고갈

    EXPECT_NE(a, nullptr);
    EXPECT_NE(b, nullptr);
    EXPECT_EQ(c, nullptr);
    EXPECT_EQ(pool.in_use(), 2u);
}

// ---- release ----
TEST(ObjectPool, ReleaseRestoresSlot) {
    ObjectPool<int> pool{2};

    int* a = pool.acquire();
    EXPECT_EQ(pool.in_use(), 1u);

    pool.release(a);
    EXPECT_EQ(pool.in_use(), 0u);
    EXPECT_EQ(pool.available(), 2u);
}

TEST(ObjectPool, ReleaseAllAndReacquire) {
    ObjectPool<int> pool{3};

    int* a = pool.acquire();
    int* b = pool.acquire();
    int* c = pool.acquire();

    pool.release(a);
    pool.release(b);
    pool.release(c);
    EXPECT_EQ(pool.in_use(), 0u);

    EXPECT_NE(pool.acquire(), nullptr);
    EXPECT_NE(pool.acquire(), nullptr);
    EXPECT_NE(pool.acquire(), nullptr);
    EXPECT_EQ(pool.in_use(), 3u);
}

// ---- LIFO 순서 (캐시 친화) ----
TEST(ObjectPool, LifoOrder) {
    ObjectPool<int> pool{3};

    int* a = pool.acquire();
    int* b = pool.acquire();

    pool.release(a);
    pool.release(b);

    // 마지막에 release된 b가 먼저 나와야 함
    int* x = pool.acquire();
    int* y = pool.acquire();

    EXPECT_EQ(x, b);
    EXPECT_EQ(y, a);
}

// ---- 획득 포인터 유일성 ----
TEST(ObjectPool, AcquiredPointersAreUnique) {
    constexpr std::size_t N = 8;
    ObjectPool<int> pool{N};
    std::set<int*> seen;

    for (std::size_t i = 0; i < N; ++i) {
        int* p = pool.acquire();
        ASSERT_NE(p, nullptr);
        auto [_, inserted] = seen.insert(p);
        EXPECT_TRUE(inserted) << "duplicate pointer at i=" << i;
    }
}

// ---- 객체 사용 (write/read) ----
TEST(ObjectPool, AcquiredObjectIsWritable) {
    ObjectPool<int> pool{2};

    int* p = pool.acquire();
    ASSERT_NE(p, nullptr);

    *p = 42;
    EXPECT_EQ(*p, 42);

    pool.release(p);

    // 재사용 시 이전 값 보존됨 (v01은 재초기화 안 함)
    int* q = pool.acquire();
    EXPECT_EQ(q, p); // LIFO로 같은 슬롯
    EXPECT_EQ(*q, 42);
}

// ---- 비POD 타입 ----
TEST(ObjectPool, WorksWithNonPodType) {
    struct Item {
        int x{0};
        std::vector<int> data;
    };

    ObjectPool<Item> pool{2};

    Item* p = pool.acquire();
    ASSERT_NE(p, nullptr);

    p->x = 7;
    p->data.push_back(100);

    pool.release(p);
    EXPECT_EQ(pool.in_use(), 0u);
}

// ---- Move semantics ----
TEST(ObjectPool, MoveConstruct) {
    ObjectPool<int> a{3};
    int* p = a.acquire();
    EXPECT_EQ(a.in_use(), 1u);

    ObjectPool<int> b{std::move(a)};
    EXPECT_EQ(b.in_use(), 1u);
    EXPECT_EQ(b.capacity(), 3u);

    b.release(p);
    EXPECT_EQ(b.in_use(), 0u);
}

// ---- 경계 ----
TEST(ObjectPool, CapacityOneExhaustAndRestore) {
    ObjectPool<int> pool{1};

    int* a = pool.acquire();
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(pool.acquire(), nullptr);

    pool.release(a);
    int* b = pool.acquire();
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b, a);
}
