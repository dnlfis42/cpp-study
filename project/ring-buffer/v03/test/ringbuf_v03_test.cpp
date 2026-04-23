#include "ringbuf/v03/ring_buffer.hpp"

#include <gtest/gtest.h>

#include <array>
#include <vector>

#include <cstddef>
#include <cstdint>
#include <cstring>

using ringbuf::v03::RingBuffer;

namespace {
std::vector<std::byte> make_seq(std::size_t n, std::uint8_t start = 0) {
    std::vector<std::byte> v(n);
    for (std::size_t i = 0; i < n; ++i) {
        v[i] = std::byte{static_cast<std::uint8_t>(start + i)};
    }
    return v;
}
} // namespace

// ---- 컴파일 타임 capacity ----
TEST(RingBuffer, CapacityIsCompileTime) {
    static_assert(RingBuffer<16>::capacity() == 16u);
    static_assert(RingBuffer<64>::capacity() == 64u);
}

// ---- 생성/상태 ----
TEST(RingBuffer, InitialState) {
    RingBuffer<16> rb;

    EXPECT_EQ(rb.capacity(), 16u);
    EXPECT_EQ(rb.size(), 0u);
    EXPECT_EQ(rb.available(), 16u);
    EXPECT_TRUE(rb.empty());
    EXPECT_FALSE(rb.full());
}

TEST(RingBuffer, ClearResetsState) {
    RingBuffer<8> rb;

    auto data = make_seq(5);
    rb.write(data.data(), data.size());
    rb.clear();

    EXPECT_TRUE(rb.empty());
    EXPECT_EQ(rb.size(), 0u);
    EXPECT_EQ(rb.available(), 8u);
}

// ---- write/read 기본 ----
TEST(RingBuffer, WriteThenRead) {
    RingBuffer<16> rb;

    auto in = make_seq(5);
    EXPECT_TRUE(rb.write(in.data(), in.size()));
    EXPECT_EQ(rb.size(), 5u);

    std::array<std::byte, 5> out{};
    EXPECT_TRUE(rb.read(out.data(), out.size()));
    EXPECT_EQ(std::memcmp(in.data(), out.data(), 5), 0);
    EXPECT_TRUE(rb.empty());
}

TEST(RingBuffer, WriteBeyondCapacityReturnsFalse) {
    RingBuffer<4> rb;

    auto in = make_seq(10);
    EXPECT_FALSE(rb.write(in.data(), in.size()));

    EXPECT_TRUE(rb.empty());
    EXPECT_EQ(rb.available(), 4u);
}

TEST(RingBuffer, ReadMoreThanAvailableReturnsFalse) {
    RingBuffer<16> rb;

    auto in = make_seq(3);
    rb.write(in.data(), in.size());

    std::array<std::byte, 10> out{};
    EXPECT_FALSE(rb.read(out.data(), out.size()));

    EXPECT_EQ(rb.size(), 3u);
}

TEST(RingBuffer, ReadFromEmpty) {
    RingBuffer<8> rb;

    std::array<std::byte, 5> out{};
    EXPECT_FALSE(rb.read(out.data(), out.size()));
}

TEST(RingBuffer, WriteToFull) {
    RingBuffer<4> rb;

    auto in = make_seq(4);
    rb.write(in.data(), in.size());
    EXPECT_TRUE(rb.full());

    std::byte more{99};
    EXPECT_FALSE(rb.write(&more, 1));
}

// ---- peek ----
TEST(RingBuffer, PeekDoesNotConsume) {
    RingBuffer<8> rb;

    auto in = make_seq(4);
    rb.write(in.data(), in.size());

    std::array<std::byte, 4> out{};
    EXPECT_TRUE(rb.peek(out.data(), out.size()));
    EXPECT_EQ(std::memcmp(in.data(), out.data(), 4), 0);
    EXPECT_EQ(rb.size(), 4u);

    std::array<std::byte, 4> out2{};
    EXPECT_TRUE(rb.read(out2.data(), out2.size()));
    EXPECT_EQ(std::memcmp(in.data(), out2.data(), 4), 0);
}

TEST(RingBuffer, PeekReturnsFalseOnUnderflow) {
    RingBuffer<8> rb;

    std::array<std::byte, 5> out{};
    EXPECT_FALSE(rb.peek(out.data(), out.size()));
}

// ---- wrap 동작 ----
TEST(RingBuffer, WrapAroundWrite) {
    RingBuffer<8> rb;

    auto a = make_seq(6, 0);
    rb.write(a.data(), a.size());

    std::array<std::byte, 4> dump{};
    rb.read(dump.data(), dump.size());

    auto b = make_seq(5, 100);
    EXPECT_TRUE(rb.write(b.data(), b.size()));
    EXPECT_EQ(rb.size(), 7u);

    std::array<std::byte, 7> out{};
    EXPECT_TRUE(rb.read(out.data(), out.size()));

    std::array<std::byte, 7> expected{
        std::byte{4},   std::byte{5},   std::byte{100}, std::byte{101},
        std::byte{102}, std::byte{103}, std::byte{104},
    };
    EXPECT_EQ(std::memcmp(expected.data(), out.data(), 7), 0);
}

TEST(RingBuffer, WrapAroundMultipleCycles) {
    RingBuffer<4> rb;

    for (int i = 0; i < 10; ++i) {
        std::byte v{static_cast<std::uint8_t>(i)};
        EXPECT_TRUE(rb.write(&v, 1));

        std::byte out{};
        EXPECT_TRUE(rb.read(&out, 1));
        EXPECT_EQ(out, v);
    }
    EXPECT_TRUE(rb.empty());
}

// ---- zero-copy 접근 ----
TEST(RingBuffer, WritableSizeBeforeWrap) {
    RingBuffer<8> rb;

    EXPECT_EQ(rb.writable_size(), 8u);

    auto in = make_seq(3);
    rb.write(in.data(), in.size());

    EXPECT_EQ(rb.writable_size(), 5u);
}

TEST(RingBuffer, WritableSizeAcrossWrap) {
    RingBuffer<8> rb;

    auto in = make_seq(6);
    rb.write(in.data(), in.size());

    std::array<std::byte, 4> dump{};
    rb.read(dump.data(), dump.size());

    EXPECT_EQ(rb.writable_size(), 2u);
    EXPECT_EQ(rb.available(), 6u);
}

TEST(RingBuffer, MoveWritePosAdvancesWritePos) {
    RingBuffer<8> rb;

    auto* ptr = rb.write_ptr();
    auto in = make_seq(3);
    std::memcpy(ptr, in.data(), in.size());
    EXPECT_TRUE(rb.move_write_pos(in.size()));

    EXPECT_EQ(rb.size(), 3u);

    std::array<std::byte, 3> out{};
    EXPECT_TRUE(rb.read(out.data(), out.size()));
    EXPECT_EQ(std::memcmp(in.data(), out.data(), 3), 0);
}

TEST(RingBuffer, MoveWritePosReturnsFalseOnOverflow) {
    RingBuffer<4> rb;

    EXPECT_FALSE(rb.move_write_pos(10));
    EXPECT_EQ(rb.size(), 0u);
    EXPECT_EQ(rb.available(), 4u);
}

TEST(RingBuffer, ReadableSizeAcrossWrap) {
    RingBuffer<8> rb;

    auto in = make_seq(6);
    rb.write(in.data(), in.size());

    std::array<std::byte, 4> dump{};
    rb.read(dump.data(), dump.size());

    rb.write(in.data(), 4);

    EXPECT_EQ(rb.readable_size(), 4u);
    EXPECT_EQ(rb.size(), 6u);
}

TEST(RingBuffer, MoveReadPosAdvancesReadPos) {
    RingBuffer<8> rb;

    auto in = make_seq(5);
    rb.write(in.data(), in.size());

    const auto* ptr = rb.read_ptr();
    std::array<std::byte, 5> out{};
    std::memcpy(out.data(), ptr, 5);
    EXPECT_TRUE(rb.move_read_pos(5));

    EXPECT_TRUE(rb.empty());
    EXPECT_EQ(std::memcmp(in.data(), out.data(), 5), 0);
}

TEST(RingBuffer, MoveReadPosReturnsFalseOnUnderflow) {
    RingBuffer<8> rb;

    rb.move_write_pos(3);
    EXPECT_FALSE(rb.move_read_pos(10));
    EXPECT_EQ(rb.size(), 3u);
}

TEST(RingBuffer, ReadPtrIsConst) {
    RingBuffer<16> rb;

    static_assert(std::is_same_v<decltype(rb.read_ptr()), const std::byte*>);
    (void)rb;
}

// ---- 교차 ----
TEST(RingBuffer, InterleavedWriteRead) {
    RingBuffer<4> rb;

    auto a = make_seq(3, 0);
    rb.write(a.data(), a.size());

    std::array<std::byte, 2> out1{};
    rb.read(out1.data(), out1.size());

    auto b = make_seq(3, 100);
    rb.write(b.data(), b.size());

    std::array<std::byte, 4> out2{};
    EXPECT_TRUE(rb.read(out2.data(), out2.size()));

    std::array<std::byte, 4> expected{
        std::byte{2},
        std::byte{100},
        std::byte{101},
        std::byte{102},
    };
    EXPECT_EQ(std::memcmp(expected.data(), out2.data(), 4), 0);
}

// ---- 타입 구분 (template<N>의 장점) ----
TEST(RingBuffer, DifferentCapacitiesAreDistinctTypes) {
    static_assert(!std::is_same_v<RingBuffer<16>, RingBuffer<32>>);
}
