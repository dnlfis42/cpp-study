#include "objpool/v05/object_pool.hpp"

#include <gtest/gtest.h>

#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

using objpool::v05::ObjectPool;

TEST(ObjectPool, InitialStateIsEmpty) {
    ObjectPool<int> pool;

    EXPECT_EQ(pool.in_use(), 0u);
    EXPECT_EQ(pool.total_capacity(), 0u);
}

TEST(ObjectPool, AcquireIncreasesInUse) {
    ObjectPool<int> pool;

    auto h = pool.acquire();
    EXPECT_TRUE(h);
    EXPECT_EQ(pool.in_use(), 1u);
}

TEST(ObjectPool, AutoReleaseOnScope) {
    ObjectPool<int> pool;
    {
        auto h = pool.acquire();
        EXPECT_EQ(pool.in_use(), 1u);
    }
    EXPECT_EQ(pool.in_use(), 0u);
}

TEST(ObjectPool, AcquireForwardsArgs) {
    ObjectPool<int> pool;

    auto h = pool.acquire(42);
    EXPECT_EQ(*h, 42);
}

TEST(ObjectPool, AcquireReleaseCycle) {
    ObjectPool<int> pool;

    for (int i = 0; i < 10; ++i) {
        auto h = pool.acquire(i);
        EXPECT_EQ(pool.in_use(), 1u);
    }
    EXPECT_EQ(pool.in_use(), 0u);
}

TEST(ObjectPool, MultipleAcquire) {
    ObjectPool<int> pool;
    std::vector<ObjectPool<int>::Handle> holders;

    for (int i = 0; i < 20; ++i) {
        holders.push_back(pool.acquire(i));
    }
    EXPECT_EQ(pool.in_use(), 20u);

    holders.clear();
    EXPECT_EQ(pool.in_use(), 0u);
}

TEST(ObjectPool, UniquePointers) {
    ObjectPool<int> pool;
    std::set<int*> seen;
    std::vector<ObjectPool<int>::Handle> holders;

    for (int i = 0; i < 20; ++i) {
        auto h = pool.acquire();
        ASSERT_TRUE(h);
        EXPECT_TRUE(seen.insert(h.get()).second)
            << "duplicate pointer at i=" << i;
        holders.push_back(std::move(h));
    }
}

TEST(ObjectPool, SlotReuse) {
    ObjectPool<int> pool;

    int* addr = nullptr;
    {
        auto h = pool.acquire();
        addr = h.get();
    }

    auto h2 = pool.acquire();
    EXPECT_EQ(h2.get(), addr);
}

TEST(ObjectPool, AcquiredObjectIsWritable) {
    ObjectPool<int> pool;

    auto h = pool.acquire();
    *h = 99;
}

TEST(ObjectPool, DestructorCalledOnRelease) {
    struct Tracked {
        int* counter;
        explicit Tracked(int* c) : counter{c} {}
        ~Tracked() { ++(*counter); }
    };

    int dtor_count = 0;
    {
        ObjectPool<Tracked> pool;
        auto h = pool.acquire(&dtor_count);
        EXPECT_EQ(dtor_count, 0);
    }
    EXPECT_EQ(dtor_count, 1);
}

TEST(ObjectPool, CtorExceptionNoLeak) {
    struct Thrower {
        explicit Thrower(bool should_throw) {
            if (should_throw)
                throw std::runtime_error{"ctor failed"};
        }
    };

    ObjectPool<Thrower> pool;

    EXPECT_THROW(pool.acquire(true), std::runtime_error);
    EXPECT_EQ(pool.in_use(), 0u);

    auto h = pool.acquire(false);
    EXPECT_TRUE(h);
}

TEST(ObjectPool, HandleMove) {
    ObjectPool<int> pool;

    auto h1 = pool.acquire(7);
    auto h2 = std::move(h1);

    EXPECT_FALSE(h1);
    EXPECT_TRUE(h2);
    EXPECT_EQ(pool.in_use(), 1u);
    EXPECT_EQ(*h2, 7);
}

TEST(ObjectPool, HandleReset) {
    ObjectPool<int> pool;

    auto h = pool.acquire();
    EXPECT_EQ(pool.in_use(), 1u);

    h.reset();
    EXPECT_EQ(pool.in_use(), 0u);
}

TEST(ObjectPool, NonDefaultConstructible) {
    struct NoDefault {
        int x;
        explicit NoDefault(int v) : x{v} {}
    };

    ObjectPool<NoDefault> pool;
    auto h = pool.acquire(55);
    EXPECT_EQ(h->x, 55);
}
