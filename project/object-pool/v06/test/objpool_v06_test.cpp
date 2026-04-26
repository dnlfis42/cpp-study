#include "objpool/v06/object_pool.hpp"

#include <gtest/gtest.h>

#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

using objpool::v06::ObjectPool;

// ---- 초기 상태 (lazy — 아직 slab 없음) ----
TEST(ObjectPool, InitialStateIsEmpty) {
    ObjectPool<int> pool;

    EXPECT_EQ(pool.in_use(), 0u);
    EXPECT_EQ(pool.total_capacity(), 0u);
}

// ---- acquire 후 in_use 증가 ----
TEST(ObjectPool, AcquireIncreasesInUse) {
    ObjectPool<int> pool;

    auto h = pool.acquire();
    EXPECT_TRUE(h);
    EXPECT_EQ(pool.in_use(), 1u);
}

// ---- scope 종료 시 자동 반환 ----
TEST(ObjectPool, AutoReleaseOnScope) {
    ObjectPool<int> pool;
    {
        auto h = pool.acquire();
        EXPECT_EQ(pool.in_use(), 1u);
    }
    EXPECT_EQ(pool.in_use(), 0u);
}

// ---- placement new: 인자 전달 ----
TEST(ObjectPool, AcquireForwardsArgs) {
    ObjectPool<int> pool;

    auto h = pool.acquire(42);
    EXPECT_EQ(*h, 42);
}

// ---- 여러 번 acquire/release 사이클 ----
TEST(ObjectPool, AcquireReleaseCycle) {
    ObjectPool<int> pool;

    for (int i = 0; i < 10; ++i) {
        auto h = pool.acquire(i);
        EXPECT_EQ(*h, i);
        EXPECT_EQ(pool.in_use(), 1u);
    }
    EXPECT_EQ(pool.in_use(), 0u);
}

// ---- 동시 다중 acquire ----
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

// ---- 포인터 유일성 ----
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

// ---- 슬랩 재사용: release 후 같은 주소 재획득 ----
TEST(ObjectPool, SlotReuse) {
    ObjectPool<int> pool;

    int* addr = nullptr;
    {
        auto h = pool.acquire();
        addr = h.get();
    }

    // 슬랩의 첫 슬롯이 free list에 돌아갔으므로 다음 acquire에서 같은 주소.
    auto h2 = pool.acquire();
    EXPECT_EQ(h2.get(), addr);
}

// ---- 객체 쓰기/읽기 ----
TEST(ObjectPool, AcquiredObjectIsWritable) {
    ObjectPool<int> pool;

    auto h = pool.acquire();
    *h = 99;
    EXPECT_EQ(*h, 99);
}

// ---- non-trivial T: 소멸자 호출 확인 ----
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

// ---- non-trivial T: 생성자 예외 시 슬롯 leak 없음 ----
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

    // 예외 후 슬롯이 반납되어 다음 acquire 정상 동작
    EXPECT_NO_THROW({ auto h = pool.acquire(false); });
}

// ---- Handle move ----
TEST(ObjectPool, HandleMove) {
    ObjectPool<int> pool;

    auto h1 = pool.acquire(7);
    auto h2 = std::move(h1);

    EXPECT_FALSE(h1);
    EXPECT_TRUE(h2);
    EXPECT_EQ(pool.in_use(), 1u);
    EXPECT_EQ(*h2, 7);
}

// ---- Handle reset ----
TEST(ObjectPool, HandleReset) {
    ObjectPool<int> pool;

    auto h = pool.acquire();
    EXPECT_EQ(pool.in_use(), 1u);

    h.reset();
    EXPECT_EQ(pool.in_use(), 0u);
}

// ---- non-default-constructible T ----
TEST(ObjectPool, NonDefaultConstructible) {
    struct NoDefault {
        int x;
        explicit NoDefault(int v) : x{v} {}
    };

    ObjectPool<NoDefault> pool;
    auto h = pool.acquire(55);
    EXPECT_EQ(h->x, 55);
}
