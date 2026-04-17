#include "objpool/v02/object_pool.hpp"

#include <gtest/gtest.h>

#include <set>
#include <utility>
#include <vector>

#include <cstddef>

using objpool::v02::ObjectPool;

// ---- 초기 상태 ----
TEST(ObjectPool, InitialState) {
    ObjectPool<int> pool{4};

    EXPECT_EQ(pool.capacity(), 4u);
    EXPECT_EQ(pool.available(), 4u);
    EXPECT_EQ(pool.in_use(), 0u);
}

// ---- Raw API (v01과 동일 검증) ----
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
    int* c = pool.acquire();

    EXPECT_NE(a, nullptr);
    EXPECT_NE(b, nullptr);
    EXPECT_EQ(c, nullptr);
    EXPECT_EQ(pool.in_use(), 2u);
}

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
}

TEST(ObjectPool, LifoOrder) {
    ObjectPool<int> pool{3};

    int* a = pool.acquire();
    int* b = pool.acquire();

    pool.release(a);
    pool.release(b);

    int* x = pool.acquire();
    int* y = pool.acquire();

    EXPECT_EQ(x, b);
    EXPECT_EQ(y, a);
}

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

TEST(ObjectPool, MoveConstruct) {
    ObjectPool<int> a{3};
    int* p = a.acquire();
    EXPECT_EQ(a.in_use(), 1u);

    ObjectPool<int> b{std::move(a)};
    EXPECT_EQ(b.in_use(), 1u);

    b.release(p);
    EXPECT_EQ(b.in_use(), 0u);
}

// ---- RAII Handle ----
TEST(ObjectPool, HandleAutoReleasesOnScope) {
    ObjectPool<int> pool{2};
    {
        auto h = pool.acquire_unique();
        ASSERT_TRUE(h);
        EXPECT_EQ(pool.in_use(), 1u);
    }
    EXPECT_EQ(pool.in_use(), 0u);
    EXPECT_EQ(pool.available(), 2u);
}

TEST(ObjectPool, HandleEmptyOnExhaust) {
    ObjectPool<int> pool{1};

    auto h1 = pool.acquire_unique();
    auto h2 = pool.acquire_unique();

    EXPECT_TRUE(h1);
    EXPECT_FALSE(h2);
}

TEST(ObjectPool, HandleMove) {
    ObjectPool<int> pool{2};

    auto h1 = pool.acquire_unique();
    ASSERT_TRUE(h1);
    EXPECT_EQ(pool.in_use(), 1u);

    auto h2 = std::move(h1);
    EXPECT_FALSE(h1);
    EXPECT_TRUE(h2);
    EXPECT_EQ(pool.in_use(), 1u);
}

TEST(ObjectPool, HandleResetReturnsSlot) {
    ObjectPool<int> pool{2};

    auto h = pool.acquire_unique();
    EXPECT_EQ(pool.in_use(), 1u);

    h.reset();
    EXPECT_EQ(pool.in_use(), 0u);
}

TEST(ObjectPool, HandleCoexistsWithRawApi) {
    ObjectPool<int> pool{3};

    int* raw = pool.acquire();
    auto h = pool.acquire_unique();
    EXPECT_EQ(pool.in_use(), 2u);

    pool.release(raw);
    EXPECT_EQ(pool.in_use(), 1u);
}

TEST(ObjectPool, HandleWritableAndReusable) {
    ObjectPool<int> pool{2};
    int* addr = nullptr;
    {
        auto h = pool.acquire_unique();
        *h = 7;
        addr = h.get();
    }
    auto h2 = pool.acquire_unique();
    EXPECT_EQ(h2.get(), addr);
    EXPECT_EQ(*h2, 7);
}

// ---- 비POD 타입 ----
TEST(ObjectPool, WorksWithNonPodType) {
    struct Item {
        int x{0};
        std::vector<int> data;
    };

    ObjectPool<Item> pool{2};

    auto h = pool.acquire_unique();
    ASSERT_TRUE(h);
    h->x = 7;
    h->data.push_back(100);

    // 자동 release
}
