#pragma once

#include <algorithm>
#include <memory>

#include <cstddef>
#include <cstdint>
#include <cstring>

// noinline: blocks inlining; noclone: blocks IPA-CP constprop clones that
// re-insert rep movsq.
#if defined(__GNUC__) && !defined(__clang__)
#define RINGBUF_NOINLINE_NOCLONE [[gnu::noinline, gnu::noclone]]
#else
#define RINGBUF_NOINLINE_NOCLONE [[gnu::noinline]]
#endif

namespace ringbuf::v04 {

namespace detail {

RINGBUF_NOINLINE_NOCLONE
inline void copy_bytes(void* dst, const void* src, std::size_t n) noexcept {
    std::memcpy(dst, src, n);
}

} // namespace detail

// Monotonic sequence counters; size() = write_pos_ - read_pos_ (uint64 overflow
// safe).
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

public: // observer
    static constexpr std::size_t capacity() noexcept { return N; }
    std::size_t size() const noexcept {
        return static_cast<std::size_t>(write_pos_ - read_pos_);
    }
    std::size_t available() const noexcept { return N - size(); }
    std::size_t readable_size() const noexcept {
        return std::min(size(), N - (read_pos_ & (N - 1)));
    }
    std::size_t writable_size() const noexcept {
        return std::min(available(), N - (write_pos_ & (N - 1)));
    }

    bool empty() const noexcept { return read_pos_ == write_pos_; }
    bool full() const noexcept { return size() == N; }

public: // accessor
    const std::byte* read_ptr() const noexcept {
        return buf_.get() + (read_pos_ & (N - 1));
    }
    std::byte* write_ptr() noexcept {
        return buf_.get() + (write_pos_ & (N - 1));
    }

public: // modifier
    bool move_read_pos(std::size_t n) noexcept {
        if (size() < n) {
            return false;
        }
        read_pos_ += n;
        return true;
    }
    bool move_write_pos(std::size_t n) noexcept {
        if (available() < n) {
            return false;
        }
        write_pos_ += n;
        return true;
    }

    void clear() noexcept {
        read_pos_ = 0;
        write_pos_ = 0;
    }

public: // I/O
    bool peek(std::byte* dst, std::size_t n) const noexcept {
        if (size() < n) {
            return false;
        }

        const std::size_t idx = read_pos_ & (N - 1);
        const std::size_t first = std::min(n, N - idx);
        detail::copy_bytes(dst, buf_.get() + idx, first);
        const std::size_t second = n - first;
        if (second > 0) {
            detail::copy_bytes(dst + first, buf_.get(), second);
        }
        return true;
    }
    bool read(std::byte* dst, std::size_t n) noexcept {
        if (!peek(dst, n)) {
            return false;
        }

        read_pos_ += n;
        return true;
    }
    bool write(const std::byte* src, std::size_t n) noexcept {
        if (available() < n) {
            return false;
        }

        const std::size_t idx = write_pos_ & (N - 1);
        const std::size_t first = std::min(n, N - idx);
        detail::copy_bytes(buf_.get() + idx, src, first);
        const std::size_t second = n - first;
        if (second > 0) {
            detail::copy_bytes(buf_.get(), src + first, second);
        }
        write_pos_ += n;
        return true;
    }

private:
    std::unique_ptr<std::byte[]> buf_;
    std::uint64_t read_pos_{0};
    std::uint64_t write_pos_{0};
};

} // namespace ringbuf::v04
