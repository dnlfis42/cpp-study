// alignas() / hardware_destructive_interference_size 동작 검증.

#include <gtest/gtest.h>

#ifdef __cpp_lib_hardware_interference_size
#include <new>
#endif

#include <cstdint>

namespace {

struct Packed {
    std::uint64_t a;
    std::uint64_t b;
};

struct Aligned {
    alignas(64) std::uint64_t a;
    alignas(64) std::uint64_t b;
};

} // namespace

TEST(CacheAlign, PackedFitsInOneCacheLine) {
    // 16B 구조체 → 같은 cache line(64B)에 들어감 → false sharing 위험.
    EXPECT_EQ(sizeof(Packed), 16u);
}

TEST(CacheAlign, AlignedSeparatesMembers) {
    // 각 멤버가 64B 정렬 → 다른 cache line.
    Aligned obj{};
    auto base = reinterpret_cast<std::uintptr_t>(&obj);
    auto a_addr = reinterpret_cast<std::uintptr_t>(&obj.a);
    auto b_addr = reinterpret_cast<std::uintptr_t>(&obj.b);

    EXPECT_EQ(a_addr % 64u, 0u);
    EXPECT_EQ(b_addr % 64u, 0u);
    EXPECT_GE(b_addr - a_addr, 64u);
    (void)base;
}

TEST(CacheAlign, AlignedStructSizeIsAtLeastTwoCacheLines) {
    // 64B 정렬된 멤버 2개 → 최소 128B.
    EXPECT_GE(sizeof(Aligned), 128u);
}

#ifdef __cpp_lib_hardware_interference_size
TEST(CacheAlign, HardwareDestructiveInterferenceSizeIsReasonable) {
    // C++17부터 표준. 구현 정의 (보통 64).
    EXPECT_GE(std::hardware_destructive_interference_size, 16u);
    EXPECT_LE(std::hardware_destructive_interference_size, 256u);
}
#endif
