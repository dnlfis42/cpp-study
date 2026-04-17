#include "objpool/v05/object_pool.hpp"

#include <gtest/gtest.h>

#include <set>
#include <utility>
#include <vector>

#include <cstddef>

using objpool::v05::ObjectPool;

// ---- 초기 상태 (lazy — 아직 청크 없음) ----
TEST(ObjectPool, InitialStateIsEmpty) {
    ObjectPool<int> pool{4};

    EXPECT_EQ(pool.capacity(), 0u);
    EXPECT_EQ(pool.available(), 0u);
    EXPECT_EQ(pool.in_use(), 0u);
    EXPECT_EQ(pool.chunk_size(), 4u);
}

// ---- 첫 acquire가 첫 청크 생성 ----
TEST(ObjectPool, FirstAcquireAllocatesChunk) {
    ObjectPool<int> pool{4};

    auto h = pool.acquire();
    EXPECT_TRUE(h);
    EXPECT_EQ(pool.capacity(), 4u);
    EXPECT_EQ(pool.available(), 3u);
    EXPECT_EQ(pool.in_use(), 1u);
}

// ---- 청크 소진 시 자동 확장 ----
TEST(ObjectPool, AutoGrowsOnExhaust) {
    ObjectPool<int> pool{2};
    std::vector<ObjectPool<int>::Handle> holders;

    for (int i = 0; i < 5; ++i) {
        holders.push_back(pool.acquire());
        ASSERT_TRUE(holders.back());
    }

    // chunk_size=2, 5번 acquire → 3청크 (capacity = 6)
    EXPECT_EQ(pool.capacity(), 6u);
    EXPECT_EQ(pool.in_use(), 5u);
    EXPECT_EQ(pool.available(), 1u);
}

// ---- 자동 반환 ----
TEST(ObjectPool, AutoReleaseOnScope) {
    ObjectPool<int> pool{2};
    {
        auto h = pool.acquire();
        EXPECT_EQ(pool.in_use(), 1u);
    }
    EXPECT_EQ(pool.in_use(), 0u);
    EXPECT_EQ(pool.available(), 2u); // 청크 유지, 반납만
}

// ---- 성장 후 기존 포인터 안정성 (핵심) ----
TEST(ObjectPool, PointersStableAcrossGrowth) {
    ObjectPool<int> pool{2};

    auto a = pool.acquire();
    auto b = pool.acquire();
    int* a_addr = a.get();
    int* b_addr = b.get();

    // 청크 추가 유발
    auto c = pool.acquire();
    auto d = pool.acquire();

    // 기존 a, b 주소 그대로여야 함
    EXPECT_EQ(a.get(), a_addr);
    EXPECT_EQ(b.get(), b_addr);

    *a = 100;
    EXPECT_EQ(*a, 100);
}

// ---- LIFO 순서 (청크 내) ----
TEST(ObjectPool, LifoWithinChunk) {
    ObjectPool<int> pool{4};

    int* a_addr = nullptr;
    int* b_addr = nullptr;
    {
        auto a = pool.acquire();
        auto b = pool.acquire();
        a_addr = a.get();
        b_addr = b.get();
    } // b 먼저 release, a 그 다음

    auto x = pool.acquire();
    auto y = pool.acquire();
    EXPECT_EQ(x.get(), a_addr);
    EXPECT_EQ(y.get(), b_addr);
}

// ---- 포인터 유일성 (여러 청크 걸쳐서) ----
TEST(ObjectPool, UniquePointersAcrossChunks) {
    constexpr std::size_t N = 20;
    ObjectPool<int> pool{4}; // 5 청크 필요
    std::set<int*> seen;
    std::vector<ObjectPool<int>::Handle> holders;

    for (std::size_t i = 0; i < N; ++i) {
        auto h = pool.acquire();
        ASSERT_TRUE(h);
        EXPECT_TRUE(seen.insert(h.get()).second);
        holders.push_back(std::move(h));
    }
    EXPECT_EQ(pool.capacity(), 20u);
}

// ---- 객체 사용 ----
TEST(ObjectPool, AcquiredObjectIsWritable) {
    ObjectPool<int> pool{2};

    auto h = pool.acquire();
    *h = 42;
    EXPECT_EQ(*h, 42);
}

// ---- 비POD ----
TEST(ObjectPool, WorksWithNonPodType) {
    struct Item {
        int x{0};
        std::vector<int> data;
    };

    ObjectPool<Item> pool{2};

    auto h = pool.acquire();
    h->x = 7;
    h->data.push_back(100);
    EXPECT_EQ(h->x, 7);
    EXPECT_EQ(h->data[0], 100);
}

// ---- Move handle ----
TEST(ObjectPool, HandleMove) {
    ObjectPool<int> pool{2};

    auto h1 = pool.acquire();
    auto h2 = std::move(h1);

    EXPECT_FALSE(h1);
    EXPECT_TRUE(h2);
    EXPECT_EQ(pool.in_use(), 1u);
}

// ---- reset ----
TEST(ObjectPool, HandleReset) {
    ObjectPool<int> pool{2};

    auto h = pool.acquire();
    EXPECT_EQ(pool.in_use(), 1u);

    h.reset();
    EXPECT_EQ(pool.in_use(), 0u);
}

// ---- chunk_size = 1 (경계) ----
TEST(ObjectPool, ChunkSizeOne) {
    ObjectPool<int> pool{1};

    auto a = pool.acquire();
    EXPECT_EQ(pool.capacity(), 1u);

    auto b = pool.acquire();
    EXPECT_EQ(pool.capacity(), 2u);

    auto c = pool.acquire();
    EXPECT_EQ(pool.capacity(), 3u);
    EXPECT_EQ(pool.in_use(), 3u);
}
