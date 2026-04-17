#pragma once

#include <algorithm>
#include <memory>

#include <cstddef>
#include <cstring>

namespace ringbuf::v03 {

namespace detail {
// memcpy 호출을 래퍼로 감싸 compile-time 크기 특화 차단.
// - noinline: 호출 지점 인라인 금지
// - noclone: IPA-CP가 constprop clone을 만드는 것 차단
//   (noinline만으론 copy_bytes.constprop.N 클론이 생겨 본체에 rep movsq
//   재삽입됨)
// 결과: n은 항상 runtime → call memcpy@plt → glibc 런타임 dispatch
#if defined(__GNUC__) && !defined(__clang__)
#define RINGBUF_NOINLINE_NOCLONE [[gnu::noinline, gnu::noclone]]
#else
#define RINGBUF_NOINLINE_NOCLONE [[gnu::noinline]]
#endif
RINGBUF_NOINLINE_NOCLONE
inline void copy_bytes(void* dst, const void* src, std::size_t n) noexcept {
    std::memcpy(dst, src, n);
}
} // namespace detail

template <std::size_t N>
class RingBuffer {
    static_assert(N > 0, "N must be > 0");
    static_assert((N & (N - 1)) == 0, "N must be a power of 2");

public:
    RingBuffer() : buf_{new std::byte[N]} {}
    ~RingBuffer() = default;

    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;
    RingBuffer(RingBuffer&&) noexcept = default;
    RingBuffer& operator=(RingBuffer&&) noexcept = default;

public: // 상태
    static constexpr std::size_t capacity() noexcept { return N; }
    std::size_t size() const noexcept { return size_; }
    std::size_t available() const noexcept { return N - size_; }
    bool empty() const noexcept { return size_ == 0; }
    bool full() const noexcept { return size_ == N; }

public: // 상태 변경
    void clear() noexcept {
        read_pos_ = 0;
        write_pos_ = 0;
        size_ = 0;
    }

public: // 직접 접근 (zero-copy)
    const std::byte* read_ptr() const noexcept {
        return buf_.get() + read_pos_;
    }
    std::byte* write_ptr() noexcept { return buf_.get() + write_pos_; }

    std::size_t readable_size() const noexcept {
        return std::min(size_, N - read_pos_);
    }
    std::size_t writable_size() const noexcept {
        return std::min(available(), N - write_pos_);
    }

    bool move_read_pos(std::size_t n) noexcept {
        if (size_ < n) {
            return false;
        }
        read_pos_ = (read_pos_ + n) & (N - 1);
        size_ -= n;
        return true;
    }
    bool move_write_pos(std::size_t n) noexcept {
        if (available() < n) {
            return false;
        }
        write_pos_ = (write_pos_ + n) & (N - 1);
        size_ += n;
        return true;
    }

public: // raw 바이트
    bool read(std::byte* dst, std::size_t n) noexcept {
        if (!peek(dst, n)) {
            return false;
        }
        read_pos_ = (read_pos_ + n) & (N - 1);
        size_ -= n;
        return true;
    }

    bool write(const std::byte* src, std::size_t n) noexcept {
        if (available() < n) {
            return false;
        }
        const std::size_t first = std::min(n, N - write_pos_);
        detail::copy_bytes(buf_.get() + write_pos_, src, first);
        const std::size_t second = n - first;
        if (second > 0) {
            detail::copy_bytes(buf_.get(), src + first, second);
        }
        write_pos_ = (write_pos_ + n) & (N - 1);
        size_ += n;
        return true;
    }

    bool peek(std::byte* dst, std::size_t n) const noexcept {
        if (size_ < n) {
            return false;
        }
        const std::size_t first = std::min(n, N - read_pos_);
        detail::copy_bytes(dst, buf_.get() + read_pos_, first);
        const std::size_t second = n - first;
        if (second > 0) {
            detail::copy_bytes(dst + first, buf_.get(), second);
        }
        return true;
    }

private: // 멤버
    std::unique_ptr<std::byte[]> buf_;
    std::size_t read_pos_{0};
    std::size_t write_pos_{0};
    std::size_t size_{0};
};

} // namespace ringbuf::v03
