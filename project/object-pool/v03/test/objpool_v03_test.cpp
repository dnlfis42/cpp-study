#include "objpool/v03/object_pool.hpp"

#include <gtest/gtest.h>

#include <utility>
#include <vector>

using objpool::v03::ObjectPool;

TEST(ObjectPool, InitialState) {
    ObjectPool<int> pool{4};

    EXPECT_EQ(pool.capacity(), 4u);
    EXPECT_EQ(pool.available(), 4u);
    EXPECT_EQ(pool.in_use(), 0u);
}

TEST(ObjectPool, HandleAutoReleasesOnScope) {
    ObjectPool<int> pool{2};
    {
        auto h = pool.acquire();
        ASSERT_TRUE(h);
        EXPECT_EQ(pool.in_use(), 1u);
    }
    EXPECT_EQ(pool.in_use(), 0u);
    EXPECT_EQ(pool.available(), 2u);
}

TEST(ObjectPool, HandleEmptyOnExhaust) {
    ObjectPool<int> pool{1};

    auto h1 = pool.acquire();
    auto h2 = pool.acquire();

    EXPECT_TRUE(h1);
    EXPECT_FALSE(h2);
}

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

TEST(ObjectPool, HandleResetReturnsSlot) {
    ObjectPool<int> pool{2};

    auto h = pool.acquire();
    EXPECT_EQ(pool.in_use(), 1u);

    h.reset();
    EXPECT_EQ(pool.in_use(), 0u);
}

TEST(ObjectPool, HandleWritableAndReusable) {
    ObjectPool<int> pool{2};
    int* addr = nullptr;
    {
        auto h = pool.acquire();
        *h = 7;
        addr = h.get();
    }
    auto h2 = pool.acquire();
    EXPECT_EQ(h2.get(), addr);
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
