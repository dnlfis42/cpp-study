#include "mempool/v01/memory_pool.hpp"

#include <gtest/gtest.h>

#include <new>
#include <utility>

#include <cstddef>
#include <cstdint>

using mempool::v01::MemoryPool;

// ---- 초기 상태 ----

TEST(MemoryPool, InitialStateIsCorrect) {
    const std::size_t n = 64;

    MemoryPool pool{n};

    EXPECT_EQ(pool.capacity(), n);
    EXPECT_EQ(pool.available(), n);
    EXPECT_EQ(pool.in_use(), 0);
}

// ---- 기본 할당 ----

TEST(MemoryPool, AllocateReturnsNonNull) {
    const std::size_t n = 64;

    MemoryPool pool{n};

    EXPECT_NE(pool.allocate(16), nullptr);
}

TEST(MemoryPool, AllocateAdvancesPos) {
    const std::size_t n = 64;

    MemoryPool pool{n};

    [[maybe_unused]]
    auto ptr = pool.allocate(16);

    EXPECT_EQ(pool.available(), 48);
    EXPECT_EQ(pool.in_use(), 16);
}

TEST(MemoryPool, AllocateReturnsDistinctPointers) {
    const std::size_t n = 64;

    MemoryPool pool{n};

    void* p1 = pool.allocate(16);
    void* p2 = pool.allocate(16);

    EXPECT_NE(p1, p2);
}

TEST(MemoryPool, AllocateReturnsContiguousMemory) {
    const std::size_t n = 64;

    MemoryPool pool{n};

    auto* p1 = static_cast<std::byte*>(pool.allocate(16));
    auto* p2 = static_cast<std::byte*>(pool.allocate(16));

    // 16바이트 요청은 max_align_t(보통 16) 정렬에 이미 맞으므로 패딩 0
    EXPECT_EQ(p2 - p1, 16);
}

// ---- 정렬 ----

TEST(MemoryPool, DefaultAlignmentIsMaxAlign) {
    const std::size_t n = 64;

    MemoryPool pool{n};

    void* p = pool.allocate(1);

    EXPECT_EQ(
        reinterpret_cast<std::uintptr_t>(p) % alignof(std::max_align_t), 0u
    );
}

TEST(MemoryPool, ExplicitAlignment) {
    const std::size_t n = 1024;

    MemoryPool pool{n};

    void* p = pool.allocate(1, 64);

    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(p) % 64u, 0u);
}

TEST(MemoryPool, AlignmentPaddingConsumesSpace) {
    const std::size_t n = 1024;

    MemoryPool pool{n};

    pool.allocate(1); // pos = 1
    const std::size_t before = pool.in_use();
    pool.allocate(1, 64); // 1 → 64로 올림, 그 후 1바이트
    const std::size_t after = pool.in_use();

    EXPECT_EQ(after, 65u); // 패딩 63 + 데이터 1 + before(1) = 65
    EXPECT_GT(after - before, 1u);
}

TEST(MemoryPool, AlignmentOnAlreadyAlignedPos) {
    const std::size_t n = 1024;

    MemoryPool pool{n};

    pool.allocate(64, 64); // pos = 64 (이미 64 정렬)
    const std::size_t before = pool.in_use();
    void* p = pool.allocate(8, 64); // 패딩 없어야 함
    const std::size_t after = pool.in_use();

    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(p) % 64u, 0u);
    EXPECT_EQ(after - before, 8u);
}

// ---- 용량 초과 ----

TEST(MemoryPool, AllocateThrowsOnOverflow) {
    const std::size_t n = 64;

    MemoryPool pool{n};

    EXPECT_THROW(pool.allocate(128), std::bad_alloc);
}

TEST(MemoryPool, AllocateThrowsOnAlignedOverflow) {
    const std::size_t n = 32;

    MemoryPool pool{n};

    pool.allocate(1); // pos = 1
    // 정렬 올림(1 → 64)이 capacity(32) 초과
    EXPECT_THROW(pool.allocate(1, 64), std::bad_alloc);
}

TEST(MemoryPool, StateUnchangedAfterThrow) {
    const std::size_t n = 64;

    MemoryPool pool{n};

    pool.allocate(16);
    const std::size_t before = pool.in_use();

    EXPECT_THROW(pool.allocate(128), std::bad_alloc);
    EXPECT_EQ(pool.in_use(), before);
}

// ---- reset ----

TEST(MemoryPool, ResetClearsInUse) {
    const std::size_t n = 1024;

    MemoryPool pool{n};

    pool.allocate(64);
    pool.allocate(128);
    pool.reset();

    EXPECT_EQ(pool.in_use(), 0u);
    EXPECT_EQ(pool.available(), pool.capacity());
}

TEST(MemoryPool, AllocateAfterResetReusesMemory) {
    const std::size_t n = 1024;

    MemoryPool pool{n};

    void* p1 = pool.allocate(64);
    pool.reset();
    void* p2 = pool.allocate(64);

    EXPECT_EQ(p1, p2);
}

// ---- 메모리 사용 가능성 ----

TEST(MemoryPool, WrittenMemoryIsReadable) {
    const std::size_t n = 1024;

    MemoryPool pool{n};

    auto* p = static_cast<std::byte*>(pool.allocate(256));
    for (std::size_t i = 0; i < 256; ++i) {
        p[i] = static_cast<std::byte>(i & 0xFF);
    }
    for (std::size_t i = 0; i < 256; ++i) {
        EXPECT_EQ(p[i], static_cast<std::byte>(i & 0xFF));
    }
}

// ---- Move semantics ----

TEST(MemoryPool, MoveCtorTransfersOwnership) {
    const std::size_t n = 1024;

    MemoryPool src{n};
    src.allocate(128);

    MemoryPool dst{std::move(src)};

    EXPECT_EQ(dst.capacity(), n);
    EXPECT_EQ(dst.in_use(), 128u);
    EXPECT_EQ(src.capacity(), 0u);
    EXPECT_EQ(src.in_use(), 0u);
}

TEST(MemoryPool, MoveCtorOriginalIsEmpty) {
    // moved-from 원본 dtor가 munmap을 안 호출해야 double free 없음.
    // 충돌 없이 스코프 종료되면 PASS.
    const std::size_t n = 1024;

    MemoryPool src{n};
    {
        MemoryPool dst{std::move(src)};
        (void)dst;
    }
    SUCCEED();
}

TEST(MemoryPool, MoveAssignTransfersOwnership) {
    const std::size_t n_src = 1024;
    const std::size_t n_dst = 256;

    MemoryPool src{n_src};
    src.allocate(64);
    MemoryPool dst{n_dst};

    dst = std::move(src);

    EXPECT_EQ(dst.capacity(), n_src);
    EXPECT_EQ(dst.in_use(), 64u);
    EXPECT_EQ(src.capacity(), 0u);
}

TEST(MemoryPool, MoveAssignReleasesOldResource) {
    // dst가 보유하던 영역이 누수 없이 정리되는지.
    // 직접 검증은 어렵지만 충돌 없이 진행되면 PASS (asan/valgrind 보조).
    const std::size_t n_src = 1024;
    const std::size_t n_dst = 256;

    MemoryPool dst{n_dst};
    dst.allocate(64);
    MemoryPool src{n_src};

    dst = std::move(src);

    EXPECT_EQ(dst.capacity(), n_src);
    SUCCEED();
}
