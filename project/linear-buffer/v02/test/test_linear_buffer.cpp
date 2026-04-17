#include "linbuf/v02/linear_buffer.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <cstddef>
#include <cstdint>
#include <cstring>

using linbuf::v02::LinearBuffer;

// --- 초기 상태 ---
TEST(LinearBuffer, InitialState) {
    LinearBuffer lb{64};

    EXPECT_EQ(lb.capacity(), 64u);
    EXPECT_EQ(lb.size(), 0u);
    EXPECT_EQ(lb.available(), 64u);
    EXPECT_TRUE(lb.empty());
}

// --- primitive round-trip ---
TEST(LinearBuffer, IntRoundTrip) {
    LinearBuffer lb{64};

    int in{42};
    lb << in;

    EXPECT_EQ(lb.size(), sizeof(int));

    int out{};
    lb >> out;

    EXPECT_EQ(out, 42);
    EXPECT_TRUE(lb.empty());
}

TEST(LinearBuffer, MultipleTypesRoundTrip) {
    LinearBuffer lb{128};

    bool b{true};
    int i{-7};
    unsigned long long ull{0xDEADBEEFCAFEBABEull};
    double d{3.141592};
    lb << b << i << ull << d;

    EXPECT_EQ(lb.size(), sizeof(b) + sizeof(i) + sizeof(ull) + sizeof(d));

    bool b_out{false};
    int i_out{};
    unsigned long long ull_out{};
    double d_out{};
    lb >> b_out >> i_out >> ull_out >> d_out;

    EXPECT_EQ(b_out, true);
    EXPECT_EQ(i_out, -7);
    EXPECT_EQ(ull_out, 0xDEADBEEFCAFEBABEull);
    EXPECT_DOUBLE_EQ(d_out, 3.141592);
    EXPECT_TRUE(lb.empty());
}

TEST(LinearBuffer, Char8RoundTrip) {
    LinearBuffer lb{16};

    char8_t in{u8'A'};
    lb << in;

    char8_t out{};
    lb >> out;

    EXPECT_EQ(out, u8'A');
    EXPECT_TRUE(lb.empty());
}

// --- 공간 부족 throw ---
TEST(LinearBuffer, WriteOverflowThrow) {
    LinearBuffer lb{3};

    int v{1};

    EXPECT_THROW(lb << v, std::runtime_error);
}

TEST(LinearBuffer, ReadUnderflowThrow) {
    LinearBuffer lb{64};

    int out{};

    EXPECT_THROW(lb >> out, std::runtime_error);
}

// --- raw write/read ---
TEST(LinearBuffer, RawWriteReadSuccess) {
    LinearBuffer lb{64};

    std::byte src[4]{
        std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04}
    };

    EXPECT_TRUE(lb.write(src, sizeof(src)));
    EXPECT_EQ(lb.size(), 4u);

    std::byte dst[4]{};

    EXPECT_TRUE(lb.read(dst, sizeof(dst)));
    EXPECT_EQ(std::memcmp(src, dst, 4), 0);
    EXPECT_TRUE(lb.empty());
}

TEST(LinearBuffer, RawWriteReturnsFalseOnOverflow) {
    LinearBuffer lb{2};

    std::byte src[4]{};

    EXPECT_FALSE(lb.write(src, 4));
    EXPECT_EQ(lb.size(), 0u);
    EXPECT_EQ(lb.available(), 2u);
}

TEST(LinearBuffer, RawReadReturnsFalseOnUnderflow) {
    LinearBuffer lb{64};

    std::byte dst[4]{};

    EXPECT_FALSE(lb.read(dst, 4));
    EXPECT_EQ(lb.size(), 0u);
}

// --- peek ---
TEST(LinearBuffer, PeekDoesNotConsume) {
    LinearBuffer lb{64};

    int v{99};
    lb << v;

    std::byte out[sizeof(int)]{};

    EXPECT_TRUE(lb.peek(out, sizeof(int)));
    EXPECT_EQ(lb.size(), sizeof(int));
}

TEST(LinearBuffer, PeekReturnsFalseOnUnderflow) {
    LinearBuffer lb{64};

    std::byte dst[4]{};

    EXPECT_FALSE(lb.peek(dst, 4));
}

// --- clear ---
TEST(LinearBuffer, ClearResetsPositions) {
    LinearBuffer lb{64};

    lb << 1 << 2 << 3;

    EXPECT_EQ(lb.size(), 3u * sizeof(int));

    lb.clear();

    EXPECT_EQ(lb.size(), 0u);
    EXPECT_EQ(lb.available(), 64u);
    EXPECT_TRUE(lb.empty());
}

// --- zero-copy ---
TEST(LinearBuffer, MoveWritePosAfterExternalFill) {
    LinearBuffer lb{64};

    int src{1234};
    std::memcpy(lb.write_ptr(), &src, sizeof(int));

    EXPECT_TRUE(lb.move_write_pos(sizeof(int)));

    int out{};
    lb >> out;

    EXPECT_EQ(out, 1234);
}

TEST(LinearBuffer, MoveWritePosReturnsFalseOnOverflow) {
    LinearBuffer lb{4};

    EXPECT_FALSE(lb.move_write_pos(10));
    EXPECT_EQ(lb.size(), 0u);
    EXPECT_EQ(lb.available(), 4u);
}

TEST(LinearBuffer, MoveReadPosReturnsFalseOnOverflow) {
    LinearBuffer lb{8};

    lb.move_write_pos(3);

    EXPECT_FALSE(lb.move_read_pos(10));
    EXPECT_EQ(lb.size(), 3u);
}

TEST(LinearBuffer, ReadPtrIsConst) {
    LinearBuffer lb{16};

    static_assert(std::is_same_v<decltype(lb.read_ptr()), const std::byte*>);
    (void)lb;
}

// --- v02 신규: read_span (전체 읽기 영역 뷰) ---
TEST(LinearBuffer, ReadSpanReturnsCurrentData) {
    LinearBuffer lb{64};

    std::int32_t v{0x12345678};
    lb << v;

    auto span = lb.read_span();

    EXPECT_EQ(span.size(), sizeof(std::int32_t));

    std::int32_t recovered{};
    std::memcpy(&recovered, span.data(), sizeof(std::int32_t));

    EXPECT_EQ(recovered, v);
    EXPECT_EQ(lb.size(), sizeof(std::int32_t)); // 변경 없음
}

TEST(LinearBuffer, ReadSpanEmptyWhenNoData) {
    LinearBuffer lb{64};

    auto span = lb.read_span();

    EXPECT_TRUE(span.empty());
}

TEST(LinearBuffer, ReadSpanAfterPartialRead) {
    LinearBuffer lb{64};

    std::int32_t a{1}, b{2};
    lb << a << b;

    std::int32_t tmp{};
    lb >> tmp; // a 소비

    auto span = lb.read_span();

    EXPECT_EQ(span.size(), sizeof(std::int32_t)); // b만 남음

    std::int32_t recovered{};
    std::memcpy(&recovered, span.data(), sizeof(std::int32_t));
    EXPECT_EQ(recovered, 2);
}

// --- v02 신규: read(n) — span 반환 + 소비 ---
TEST(LinearBuffer, ReadSpanConsumeReturnsAndAdvances) {
    LinearBuffer lb{64};

    std::int32_t v{42};
    lb << v;

    auto span = lb.read(sizeof(std::int32_t));

    EXPECT_EQ(span.size(), sizeof(std::int32_t));
    EXPECT_TRUE(lb.empty());

    std::int32_t recovered{};
    std::memcpy(&recovered, span.data(), sizeof(std::int32_t));
    EXPECT_EQ(recovered, 42);
}

TEST(LinearBuffer, ReadSpanConsumeEmptyOnUnderflow) {
    LinearBuffer lb{64};

    std::int32_t v{1};
    lb << v;

    auto span = lb.read(sizeof(std::int32_t) + 1);

    EXPECT_TRUE(span.empty());
    EXPECT_EQ(lb.size(), sizeof(std::int32_t)); // 상태 변경 없음
}

TEST(LinearBuffer, ReadSpanConsumeMultipleChunks) {
    LinearBuffer lb{64};

    std::int32_t a{10}, b{20}, c{30};
    lb << a << b << c;

    auto s1 = lb.read(sizeof(std::int32_t));
    EXPECT_EQ(s1.size(), sizeof(std::int32_t));

    auto s2 = lb.read(sizeof(std::int32_t) * 2);
    EXPECT_EQ(s2.size(), sizeof(std::int32_t) * 2);
    EXPECT_TRUE(lb.empty());

    std::int32_t ra{}, rb{}, rc{};
    std::memcpy(&ra, s1.data(), sizeof(std::int32_t));
    std::memcpy(&rb, s2.data(), sizeof(std::int32_t));
    std::memcpy(&rc, s2.data() + sizeof(std::int32_t), sizeof(std::int32_t));

    EXPECT_EQ(ra, 10);
    EXPECT_EQ(rb, 20);
    EXPECT_EQ(rc, 30);
}

// --- v02 신규: peek(n) — span 반환, 비소비 ---
TEST(LinearBuffer, PeekSpanReturnsWithoutAdvance) {
    LinearBuffer lb{64};

    std::int32_t v{99};
    lb << v;

    auto span = lb.peek(sizeof(std::int32_t));

    EXPECT_EQ(span.size(), sizeof(std::int32_t));
    EXPECT_EQ(lb.size(), sizeof(std::int32_t)); // 변경 없음

    std::int32_t recovered{};
    std::memcpy(&recovered, span.data(), sizeof(std::int32_t));
    EXPECT_EQ(recovered, 99);
}

TEST(LinearBuffer, PeekSpanEmptyOnUnderflow) {
    LinearBuffer lb{64};

    auto span = lb.peek(std::size_t{4});

    EXPECT_TRUE(span.empty());
}

// --- 사용자 타입 확장 ---
namespace {
struct Login {
    std::uint32_t user_id;
    std::uint64_t session;
};

LinearBuffer& operator<<(LinearBuffer& lb, const Login& msg) {
    return lb << msg.user_id << msg.session;
}
LinearBuffer& operator>>(LinearBuffer& lb, Login& msg) {
    return lb >> msg.user_id >> msg.session;
}
} // namespace

TEST(LinearBuffer, UserDefinedTypeExtension) {
    LinearBuffer lb{64};

    Login in{12345u, 0xAABBCCDDEEFF0011ull};
    lb << in;

    Login out{};
    lb >> out;

    EXPECT_EQ(out.user_id, in.user_id);
    EXPECT_EQ(out.session, in.session);
}

TEST(LinearBuffer, ChainedUserTypes) {
    LinearBuffer lb{128};

    Login a{1u, 100ull};
    Login b{2u, 200ull};
    lb << a << b;

    Login a_out{};
    Login b_out{};
    lb >> a_out >> b_out;

    EXPECT_EQ(a_out.user_id, 1u);
    EXPECT_EQ(a_out.session, 100ull);
    EXPECT_EQ(b_out.user_id, 2u);
    EXPECT_EQ(b_out.session, 200ull);
}

// --- Move ---
TEST(LinearBuffer, MoveConstruct) {
    LinearBuffer a{32};
    a << 42;

    LinearBuffer b{std::move(a)};

    EXPECT_EQ(b.capacity(), 32u);
    EXPECT_EQ(b.size(), sizeof(int));

    int out{};
    b >> out;

    EXPECT_EQ(out, 42);
}

// --- 사용자 정의 프로토콜: string_view ---
namespace {
LinearBuffer& operator>>(LinearBuffer& lb, std::string_view& out) {
    std::uint8_t len{};
    lb >> len; // 1바이트 부족 시 throw

    auto span = lb.read(len);
    if (span.empty() && len > 0) {
        throw std::runtime_error("LinearBuffer: string payload underflow");
    }

    out = std::string_view{
        reinterpret_cast<const char*>(span.data()), span.size()
    };
    return lb;
}
LinearBuffer& operator<<(LinearBuffer& lb, std::string_view sv) {
    if (sv.size() > 0xFF) {
        throw std::runtime_error("LinearBuffer: string length exceeds uint8_t");
    }
    auto len = static_cast<std::uint8_t>(sv.size());
    lb << len;
    lb.write(reinterpret_cast<const std::byte*>(sv.data()), sv.size());
    return lb;
}
} // namespace

TEST(LinearBuffer, StringViewRoundTrip) {
    LinearBuffer lb{64};

    lb << std::string_view{"hello"};

    EXPECT_EQ(lb.size(), 1u + 5u);

    std::string_view out;
    lb >> out;

    EXPECT_EQ(out, "hello");
    EXPECT_TRUE(lb.empty());
}

TEST(LinearBuffer, StringViewEmpty) {
    LinearBuffer lb{64};

    lb << std::string_view{""};

    EXPECT_EQ(lb.size(), 1u);

    std::string_view out{"dirty"};
    lb >> out;

    EXPECT_TRUE(out.empty());
    EXPECT_TRUE(lb.empty());
}

TEST(LinearBuffer, StringViewChained) {
    LinearBuffer lb{64};

    lb << std::string_view{"foo"} << std::string_view{"bar"}
       << std::string_view{"baz"};

    std::string_view a, b, c;
    lb >> a >> b >> c;

    EXPECT_EQ(a, "foo");
    EXPECT_EQ(b, "bar");
    EXPECT_EQ(c, "baz");
    EXPECT_TRUE(lb.empty());
}

TEST(LinearBuffer, StringViewZeroCopy) {
    LinearBuffer lb{64};

    lb << std::string_view{"zero-copy"};

    const auto* expected_ptr = reinterpret_cast<const char*>(lb.read_ptr()) + 1;

    std::string_view out;
    lb >> out;

    EXPECT_EQ(out.data(), expected_ptr);
    EXPECT_EQ(out, "zero-copy");
}

TEST(LinearBuffer, StringViewWriteOverflowThrow) {
    LinearBuffer lb{1024};

    std::string too_long(256, 'x');
    EXPECT_THROW(lb << std::string_view{too_long}, std::runtime_error);
}

TEST(LinearBuffer, StringViewReadLengthUnderflowThrow) {
    LinearBuffer lb{64};

    std::string_view out;
    EXPECT_THROW(lb >> out, std::runtime_error);
}

TEST(LinearBuffer, StringViewReadPayloadUnderflowThrow) {
    LinearBuffer lb{64};

    std::uint8_t fake_len{5};
    lb << fake_len;

    std::string_view out;
    EXPECT_THROW(lb >> out, std::runtime_error);
}

TEST(LinearBuffer, StringViewMaxLength) {
    LinearBuffer lb{1024};

    std::string max_len(0xFF, 'x');
    lb << std::string_view{max_len};

    EXPECT_EQ(lb.size(), 1u + 0xFFu);

    std::string_view out;
    lb >> out;

    EXPECT_EQ(out.size(), 0xFFu);
    EXPECT_EQ(out, max_len);
}
