#pragma once

#include <algorithm>
#include <memory>

#include <cstddef>
#include <cstring>

namespace ringbuf::v01 {

class RingBuffer {
public:
    explicit RingBuffer(std::size_t capacity)
        : buf_{new std::byte[capacity]}, capacity_{capacity} {}
    ~RingBuffer() = default;

    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;
    RingBuffer(RingBuffer&&) noexcept = default;
    RingBuffer& operator=(RingBuffer&&) noexcept = default;

public: // 상태
    std::size_t capacity() const noexcept { return capacity_; }
    std::size_t size() const noexcept { return size_; }
    std::size_t available() const noexcept { return capacity_ - size_; }
    bool empty() const noexcept { return size_ == 0; }
    bool full() const noexcept { return size_ == capacity_; }

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

    // 현재 read_pos에서 연속으로 읽을 수 있는 바이트 수 (wrap 전까지)
    std::size_t readable_size() const noexcept {
        return std::min(size_, capacity_ - read_pos_);
    }
    // 현재 write_pos에서 연속으로 쓸 수 있는 바이트 수 (wrap 전까지)
    std::size_t writable_size() const noexcept {
        return std::min(available(), capacity_ - write_pos_);
    }

    bool move_read_pos(std::size_t n) noexcept {
        if (size_ < n) {
            return false;
        }
        read_pos_ = (read_pos_ + n) % capacity_;
        size_ -= n;
        return true;
    }
    bool move_write_pos(std::size_t n) noexcept {
        if (available() < n) {
            return false;
        }
        write_pos_ = (write_pos_ + n) % capacity_;
        size_ += n;
        return true;
    }

public: // raw 바이트
    bool read(std::byte* dst, std::size_t n) noexcept {
        if (!peek(dst, n)) {
            return false;
        }
        read_pos_ = (read_pos_ + n) % capacity_;
        size_ -= n;
        return true;
    }

    bool write(const std::byte* src, std::size_t n) noexcept {
        if (available() < n) {
            return false;
        }
        const std::size_t first = std::min(n, capacity_ - write_pos_);
        std::memcpy(buf_.get() + write_pos_, src, first);
        const std::size_t second = n - first;
        if (second > 0) {
            std::memcpy(buf_.get(), src + first, second);
        }
        write_pos_ = (write_pos_ + n) % capacity_;
        size_ += n;
        return true;
    }

    bool peek(std::byte* dst, std::size_t n) const noexcept {
        if (size_ < n) {
            return false;
        }
        const std::size_t first = std::min(n, capacity_ - read_pos_);
        std::memcpy(dst, buf_.get() + read_pos_, first);
        const std::size_t second = n - first;
        if (second > 0) {
            std::memcpy(dst + first, buf_.get(), second);
        }
        return true;
    }

private: // 멤버
    std::unique_ptr<std::byte[]> buf_;
    std::size_t capacity_;
    std::size_t read_pos_{0};
    std::size_t write_pos_{0};
    std::size_t size_{0};
};

} // namespace ringbuf::v01
