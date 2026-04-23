#include "mempool/v03/memory_pool.hpp"

#include <gtest/gtest.h>

#include <new>
#include <utility>
#include <vector>

#include <cstddef>
#include <cstdint>

using mempool::v03::MemoryPool;

// ---- 초기 상태 ----

TEST(MemoryPoolV03, InitialStateIsEmpty) {
    MemoryPool pool;

    EXPECT_EQ(pool.total_capacity(), 0u); // lazy: 첫 allocate 전엔 slab 없음
}

// ---- 기본 할당 ----

TEST(MemoryPoolV03, AllocateReturnsNonNull) {
    MemoryPool pool;

    EXPECT_NE(pool.allocate(16), nullptr);
}

TEST(MemoryPoolV03, AllocateGrowsCapacityBySlab) {
    MemoryPool pool;

    pool.allocate(16);

    EXPECT_EQ(pool.total_capacity(), MemoryPool::kSlabSize);
}

TEST(MemoryPoolV03, AllocateReturnsDistinctPointers) {
    MemoryPool pool;

    void* p1 = pool.allocate(16);
    void* p2 = pool.allocate(16);

    EXPECT_NE(p1, p2);
}

TEST(MemoryPoolV03, AllocateZeroThrows) {
    MemoryPool pool;

    EXPECT_THROW(pool.allocate(0), std::bad_alloc);
}

TEST(MemoryPoolV03, AllocateBeyondMaxThrows) {
    MemoryPool pool;

    EXPECT_THROW(pool.allocate(MemoryPool::kMaxSize + 1), std::bad_alloc);
}

// ---- size class 라우팅 ----

TEST(MemoryPoolV03, SizeClassBoundaries) {
    MemoryPool pool;

    // 16, 17 → 다른 클래스
    void* p16 = pool.allocate(16);
    void* p17 = pool.allocate(17);
    pool.deallocate(p16, 16);
    pool.deallocate(p17, 17);
    void* p16_again = pool.allocate(16);
    EXPECT_EQ(p16_again, p16); // 16 클래스 LIFO
    pool.deallocate(p16_again, 16);

    // 한 번이라도 alloc되었으니 16, 32 클래스 각각 1 slab
    EXPECT_EQ(pool.total_capacity(), 2u * MemoryPool::kSlabSize);
}

TEST(MemoryPoolV03, MaxSizeFitsInLargestClass) {
    MemoryPool pool;

    void* p = pool.allocate(MemoryPool::kMaxSize); // 1024 → 1024B 클래스
    pool.deallocate(p, MemoryPool::kMaxSize);

    void* p2 = pool.allocate(MemoryPool::kMaxSize);
    EXPECT_EQ(p2, p); // LIFO 같은 슬롯 반환
}

TEST(MemoryPoolV03, EachClassIndependent) {
    MemoryPool pool;

    void* p16 = pool.allocate(16);
    void* p32 = pool.allocate(32);
    void* p1024 = pool.allocate(1024);

    pool.deallocate(p32, 32); // 32 클래스에 push
    pool.deallocate(p16, 16);
    pool.deallocate(p1024, 1024);

    // 클래스별 독립적이라 각자 LIFO
    EXPECT_EQ(pool.allocate(16), p16);
    EXPECT_EQ(pool.allocate(32), p32);
    EXPECT_EQ(pool.allocate(1024), p1024);
}

// ---- 정렬 ----

TEST(MemoryPoolV03, AllocationIsAlignedToSlotSize) {
    MemoryPool pool;

    // 각 클래스에서 alloc → ptr이 slot_size 정렬되는지
    void* p16 = pool.allocate(16);
    void* p64 = pool.allocate(64);
    void* p1024 = pool.allocate(1024);

    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(p16) % 16u, 0u);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(p64) % 64u, 0u);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(p1024) % 1024u, 0u);
}

// ---- LIFO free list ----

TEST(MemoryPoolV03, DeallocReallocReturnsSameSlot) {
    MemoryPool pool;

    void* p = pool.allocate(64);
    pool.deallocate(p, 64);
    void* p2 = pool.allocate(64);

    EXPECT_EQ(p, p2);
}

TEST(MemoryPoolV03, DeallocOrderIsLIFO) {
    MemoryPool pool;

    void* p1 = pool.allocate(64);
    void* p2 = pool.allocate(64);
    void* p3 = pool.allocate(64);

    pool.deallocate(p1, 64);
    pool.deallocate(p2, 64);
    pool.deallocate(p3, 64);

    EXPECT_EQ(pool.allocate(64), p3);
    EXPECT_EQ(pool.allocate(64), p2);
    EXPECT_EQ(pool.allocate(64), p1);
}

// ---- slab 자동 성장 ----

TEST(MemoryPoolV03, SlabGrowsWhenExhausted) {
    MemoryPool pool;

    // 1024B 클래스: 64 KiB / 1024B = 64 슬롯
    constexpr std::size_t slot_count =
        MemoryPool::kSlabSize / MemoryPool::kMaxSize;

    std::vector<void*> ptrs;
    for (std::size_t i = 0; i < slot_count; ++i) {
        ptrs.push_back(pool.allocate(MemoryPool::kMaxSize));
    }
    EXPECT_EQ(pool.total_capacity(), MemoryPool::kSlabSize); // slab 1개

    pool.allocate(MemoryPool::kMaxSize); // 추가 → 새 slab
    EXPECT_EQ(pool.total_capacity(), 2u * MemoryPool::kSlabSize);
}

// ---- reset ----

TEST(MemoryPoolV03, ResetReleasesAllSlabs) {
    MemoryPool pool;

    pool.allocate(16);
    pool.allocate(64);
    pool.allocate(1024);
    EXPECT_GT(pool.total_capacity(), 0u);

    pool.reset();

    EXPECT_EQ(pool.total_capacity(), 0u);
}

TEST(MemoryPoolV03, AllocateAfterResetWorks) {
    MemoryPool pool;

    pool.allocate(64);
    pool.reset();

    EXPECT_NE(pool.allocate(64), nullptr);
    EXPECT_EQ(pool.total_capacity(), MemoryPool::kSlabSize);
}

// ---- 메모리 사용 가능성 ----

TEST(MemoryPoolV03, WrittenMemoryIsReadable) {
    MemoryPool pool;

    auto* p = static_cast<std::byte*>(pool.allocate(256));
    for (std::size_t i = 0; i < 256; ++i) {
        p[i] = static_cast<std::byte>(i & 0xFF);
    }
    for (std::size_t i = 0; i < 256; ++i) {
        EXPECT_EQ(p[i], static_cast<std::byte>(i & 0xFF));
    }
}

TEST(MemoryPoolV03, MultipleSlotsAreNonOverlapping) {
    MemoryPool pool;

    auto* p1 = static_cast<std::byte*>(pool.allocate(64));
    auto* p2 = static_cast<std::byte*>(pool.allocate(64));

    for (std::size_t i = 0; i < 64; ++i)
        p1[i] = std::byte{0xAA};
    for (std::size_t i = 0; i < 64; ++i)
        p2[i] = std::byte{0xBB};

    for (std::size_t i = 0; i < 64; ++i) {
        EXPECT_EQ(p1[i], std::byte{0xAA});
        EXPECT_EQ(p2[i], std::byte{0xBB});
    }
}

// ---- Move semantics ----

TEST(MemoryPoolV03, MoveCtorTransfersOwnership) {
    MemoryPool src;
    void* p = src.allocate(64);
    const std::size_t cap_before = src.total_capacity();

    MemoryPool dst{std::move(src)};

    EXPECT_EQ(dst.total_capacity(), cap_before);
    EXPECT_EQ(src.total_capacity(), 0u);

    // dst에서 dealloc → 같은 슬롯 재할당 가능
    dst.deallocate(p, 64);
    EXPECT_EQ(dst.allocate(64), p);
}

TEST(MemoryPoolV03, MoveAssignReleasesOldResource) {
    MemoryPool dst;
    dst.allocate(64);

    MemoryPool src;
    void* p = src.allocate(128);

    dst = std::move(src);

    EXPECT_EQ(src.total_capacity(), 0u);
    EXPECT_EQ(
        dst.total_capacity(), MemoryPool::kSlabSize
    ); // src의 128 클래스 slab
    dst.deallocate(p, 128);
    EXPECT_EQ(dst.allocate(128), p);
}

TEST(MemoryPoolV03, MoveAssignSelfNoOp) {
    MemoryPool pool;
    void* p = pool.allocate(64);

    // -Wself-move 우회: 참조 통한 우회로 self-assignment 가드 검증
    MemoryPool& ref = pool;
    pool = std::move(ref);

    pool.deallocate(p, 64);
    EXPECT_EQ(pool.allocate(64), p);
}
