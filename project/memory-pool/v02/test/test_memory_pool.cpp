#include "mempool/v02/memory_pool.hpp"

#include <gtest/gtest.h>

#include <utility>

#include <cstddef>
#include <cstdint>

using mempool::v02::MemoryPool;

// ---- 초기 상태 ----

TEST(MemoryPoolV02, InitialStateIsCorrect) {
    const std::size_t cs = 1024;

    MemoryPool pool{cs};

    EXPECT_EQ(pool.chunk_size(), cs);
    EXPECT_EQ(pool.large_threshold(), cs / 2);
    EXPECT_EQ(pool.total_in_use(), 0u);
    EXPECT_EQ(pool.total_capacity(), cs); // eager: 첫 청크 1개
}

// ---- 기본 할당 (single chunk) ----

TEST(MemoryPoolV02, AllocateReturnsNonNull) {
    const std::size_t cs = 1024;

    MemoryPool pool{cs};

    EXPECT_NE(pool.allocate(16), nullptr);
}

TEST(MemoryPoolV02, AllocateAdvancesInUse) {
    const std::size_t cs = 1024;

    MemoryPool pool{cs};

    [[maybe_unused]] auto p = pool.allocate(16);

    EXPECT_EQ(pool.total_in_use(), 16u);
}

TEST(MemoryPoolV02, AllocateReturnsDistinctPointers) {
    const std::size_t cs = 1024;

    MemoryPool pool{cs};

    void* p1 = pool.allocate(16);
    void* p2 = pool.allocate(16);

    EXPECT_NE(p1, p2);
}

TEST(MemoryPoolV02, AllocateReturnsContiguousMemory) {
    const std::size_t cs = 1024;

    MemoryPool pool{cs};

    auto* p1 = static_cast<std::byte*>(pool.allocate(16));
    auto* p2 = static_cast<std::byte*>(pool.allocate(16));

    EXPECT_EQ(p2 - p1, 16);
}

// ---- 정렬 ----

TEST(MemoryPoolV02, DefaultAlignmentIsMaxAlign) {
    const std::size_t cs = 1024;

    MemoryPool pool{cs};

    void* p = pool.allocate(1);

    EXPECT_EQ(
        reinterpret_cast<std::uintptr_t>(p) % alignof(std::max_align_t), 0u
    );
}

TEST(MemoryPoolV02, ExplicitAlignment) {
    const std::size_t cs = 1024;

    MemoryPool pool{cs};

    void* p = pool.allocate(1, 64);

    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(p) % 64u, 0u);
}

TEST(MemoryPoolV02, AlignmentPaddingConsumesSpace) {
    const std::size_t cs = 4096;

    MemoryPool pool{cs};

    pool.allocate(1); // pos = 1
    const std::size_t before = pool.total_in_use();
    pool.allocate(1, 64); // 1 → 64로 올림 + 1바이트
    const std::size_t after = pool.total_in_use();

    EXPECT_EQ(after, 65u);
    EXPECT_GT(after - before, 1u);
}

// ---- small chunk 자동 성장 ----

TEST(MemoryPoolV02, SmallChunkGrowsWhenFull) {
    const std::size_t cs = 1024;

    MemoryPool pool{cs};

    // small 한도(threshold = 512) 이하 요청을 chunk_size 넘게 누적
    pool.allocate(256);
    pool.allocate(256);
    pool.allocate(256);
    pool.allocate(256);
    // 여기까지 1024 = 첫 청크 가득

    const std::size_t cap_before = pool.total_capacity();
    pool.allocate(256); // 새 청크 트리거
    const std::size_t cap_after = pool.total_capacity();

    EXPECT_EQ(cap_after, cap_before + cs);
}

TEST(MemoryPoolV02, SmallChunkGrowthAbandonsTail) {
    // current 청크에 안 들어가면 새 청크. 이전 청크 tail은 사장.
    // total_in_use는 abandoned tail을 카운트하지 않으므로
    // 새 청크 할당 직후 in_use = 이전 청크의 실제 사용 + 새 요청.
    const std::size_t cs = 4096; // threshold = 2048

    MemoryPool pool{cs};

    pool.allocate(2000); // small, pos=2000, 남은 2096
    pool.allocate(2000); // small, pos=4000, 남은 96
    EXPECT_EQ(pool.total_in_use(), 4000u);

    pool.allocate(2000); // 2000 > 96 → 새 small 청크. tail 96 사장.
    EXPECT_EQ(pool.total_in_use(), 4000u + 2000u);
    EXPECT_EQ(pool.total_capacity(), 2u * cs);
}

// ---- large dedicated chunk ----

TEST(MemoryPoolV02, LargeRequestGoesToDedicatedChunk) {
    const std::size_t cs = 1024;

    MemoryPool pool{cs};

    const std::size_t cap_before = pool.total_capacity();
    void* p = pool.allocate(800); // 800 > threshold(512) → large
    const std::size_t cap_after = pool.total_capacity();

    EXPECT_NE(p, nullptr);
    // small 청크는 그대로, large 청크 추가됨 (페이지 정렬된 크기)
    EXPECT_GT(cap_after, cap_before);
    EXPECT_EQ(pool.total_in_use(), 800u);
}

TEST(MemoryPoolV02, LargeAllocationIsPageAligned) {
    const std::size_t cs = 1024;

    MemoryPool pool{cs};

    void* p = pool.allocate(800);

    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(p) % 4096u, 0u);
}

TEST(MemoryPoolV02, ThresholdBoundaryIsSmall) {
    // n == large_threshold 이면 small (spec: n > threshold가 large).
    const std::size_t cs = 1024;

    MemoryPool pool{cs};

    const std::size_t cap_before = pool.total_capacity();
    pool.allocate(cs / 2); // == threshold
    const std::size_t cap_after = pool.total_capacity();

    EXPECT_EQ(cap_after, cap_before); // 새 청크 없음 → small에 들어감
}

TEST(MemoryPoolV02, JustOverThresholdIsLarge) {
    const std::size_t cs = 1024;

    MemoryPool pool{cs};

    const std::size_t cap_before = pool.total_capacity();
    pool.allocate(cs / 2 + 1);
    const std::size_t cap_after = pool.total_capacity();

    EXPECT_GT(cap_after, cap_before); // large 청크 추가
}

// ---- reset: small 보존, large 해제 ----

TEST(MemoryPoolV02, ResetClearsInUse) {
    const std::size_t cs = 1024;

    MemoryPool pool{cs};

    pool.allocate(64);
    pool.allocate(128);
    pool.reset();

    EXPECT_EQ(pool.total_in_use(), 0u);
}

TEST(MemoryPoolV02, ResetPreservesSmallChunks) {
    const std::size_t cs = 4096; // threshold = 2048

    MemoryPool pool{cs};

    // small 청크 2개로 성장
    pool.allocate(2000);
    pool.allocate(2000);
    pool.allocate(2000); // 새 청크 트리거
    const std::size_t cap_before_reset = pool.total_capacity();
    EXPECT_EQ(cap_before_reset, 2u * cs);

    pool.reset();

    EXPECT_EQ(pool.total_capacity(), cap_before_reset); // 보존
    EXPECT_EQ(pool.total_in_use(), 0u);
}

TEST(MemoryPoolV02, ResetReleasesLargeChunks) {
    const std::size_t cs = 1024;

    MemoryPool pool{cs};

    const std::size_t cap_initial = pool.total_capacity(); // small 1개
    pool.allocate(800);                                    // large 추가
    EXPECT_GT(pool.total_capacity(), cap_initial);

    pool.reset();

    EXPECT_EQ(pool.total_capacity(), cap_initial); // large 해제, small만 남음
}

TEST(MemoryPoolV02, ResetReleasesLargeButPreservesSmall) {
    const std::size_t cs = 4096; // threshold = 2048

    MemoryPool pool{cs};

    pool.allocate(2000); // small 1
    pool.allocate(2000); // small 1 (남은 96)
    pool.allocate(2000); // 새 small 청크 (small 2)
    pool.allocate(3000); // 3000 > 2048 → large
    const std::size_t cap_with_large = pool.total_capacity();

    pool.reset();

    EXPECT_EQ(pool.total_in_use(), 0u);
    EXPECT_LT(pool.total_capacity(), cap_with_large); // large만큼 줄어듦
    EXPECT_EQ(pool.total_capacity(), 2u * cs);        // small 2개만 남음
}

TEST(MemoryPoolV02, AllocateAfterResetReusesFirstChunk) {
    const std::size_t cs = 1024;

    MemoryPool pool{cs};

    void* p1 = pool.allocate(64);
    pool.reset();
    void* p2 = pool.allocate(64);

    EXPECT_EQ(p1, p2); // 첫 청크 시작으로 복귀
}

TEST(MemoryPoolV02, AllocateAfterResetReusesGrownChunks) {
    // reset 후 다시 채울 때, 두 번째 청크에도 도달하는지.
    const std::size_t cs = 1024;

    MemoryPool pool{cs};

    pool.allocate(600);
    void* p2_first = pool.allocate(500); // 두 번째 청크의 첫 할당
    pool.reset();

    pool.allocate(600);
    void* p2_after = pool.allocate(500);

    EXPECT_EQ(p2_first, p2_after); // 같은 청크 같은 위치 재사용
}

// ---- 메모리 사용 가능성 ----

TEST(MemoryPoolV02, WrittenMemoryIsReadable) {
    const std::size_t cs = 4096;

    MemoryPool pool{cs};

    auto* p = static_cast<std::byte*>(pool.allocate(256));
    for (std::size_t i = 0; i < 256; ++i) {
        p[i] = static_cast<std::byte>(i & 0xFF);
    }
    for (std::size_t i = 0; i < 256; ++i) {
        EXPECT_EQ(p[i], static_cast<std::byte>(i & 0xFF));
    }
}

TEST(MemoryPoolV02, LargeAllocationIsWritable) {
    const std::size_t cs = 1024;

    MemoryPool pool{cs};

    auto* p = static_cast<std::byte*>(pool.allocate(8192));
    for (std::size_t i = 0; i < 8192; ++i) {
        p[i] = static_cast<std::byte>(i & 0xFF);
    }
    for (std::size_t i = 0; i < 8192; ++i) {
        EXPECT_EQ(p[i], static_cast<std::byte>(i & 0xFF));
    }
}

// ---- Move semantics ----

TEST(MemoryPoolV02, MoveCtorTransfersOwnership) {
    const std::size_t cs = 1024;

    MemoryPool src{cs};
    src.allocate(128);
    const std::size_t in_use_before = src.total_in_use();
    const std::size_t cap_before = src.total_capacity();

    MemoryPool dst{std::move(src)};

    EXPECT_EQ(dst.chunk_size(), cs);
    EXPECT_EQ(dst.total_in_use(), in_use_before);
    EXPECT_EQ(dst.total_capacity(), cap_before);
    EXPECT_EQ(src.total_capacity(), 0u);
    EXPECT_EQ(src.total_in_use(), 0u);
}

TEST(MemoryPoolV02, MoveCtorPreservesCurrentIndex) {
    // 청크 2개 보유 중 reset → current_=0 상태에서 move →
    // dst가 첫 청크부터 다시 bump해야 함.
    const std::size_t cs = 1024;

    MemoryPool src{cs};
    src.allocate(600);                       // 첫 청크
    src.allocate(500);                       // 두 번째 청크
    src.reset();                             // current_ = 0
    void* expected_first = src.allocate(64); // 첫 청크 시작
    src.reset();

    MemoryPool dst{std::move(src)};
    void* p = dst.allocate(64);

    EXPECT_EQ(p, expected_first); // current_ = 0이 보존되어 첫 청크부터
}

TEST(MemoryPoolV02, MoveCtorOriginalIsEmpty) {
    // moved-from 원본 dtor가 munmap을 안 호출해야 double free 없음.
    const std::size_t cs = 1024;

    MemoryPool src{cs};
    {
        MemoryPool dst{std::move(src)};
        (void)dst;
    }
    SUCCEED();
}

TEST(MemoryPoolV02, MoveAssignTransfersOwnership) {
    const std::size_t cs_src = 1024;
    const std::size_t cs_dst = 512;

    MemoryPool src{cs_src};
    src.allocate(64);
    MemoryPool dst{cs_dst};

    dst = std::move(src);

    EXPECT_EQ(dst.chunk_size(), cs_src);
    EXPECT_EQ(dst.total_in_use(), 64u);
    EXPECT_EQ(src.total_capacity(), 0u);
}

TEST(MemoryPoolV02, MoveAssignReleasesOldResource) {
    const std::size_t cs_src = 1024;
    const std::size_t cs_dst = 512;

    MemoryPool dst{cs_dst};
    dst.allocate(64);
    MemoryPool src{cs_src};

    dst = std::move(src);

    EXPECT_EQ(dst.chunk_size(), cs_src);
    SUCCEED();
}
