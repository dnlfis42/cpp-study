#pragma once

#include <memory>
#include <type_traits>

#include <cstddef>
#include <cstring>

namespace linbuf::v01 {

class LinearBuffer {
public:
    explicit LinearBuffer(std::size_t capacity)
        : buf_{new std::byte[capacity]}, capacity_{capacity} {}
    ~LinearBuffer() = default;

    LinearBuffer(const LinearBuffer&) = delete;
    LinearBuffer& operator=(const LinearBuffer&) = delete;
    LinearBuffer(LinearBuffer&&) noexcept = default;
    LinearBuffer& operator=(LinearBuffer&&) noexcept = default;

public:
    explicit operator bool() const noexcept { return !fail_; }

    template <typename T>
        requires std::is_arithmetic_v<T>
    LinearBuffer& operator<<(T v) noexcept {
        if (fail_) {
            return *this;
        }

        if (available() < sizeof(T)) {
            fail_ = true;
            return *this;
        }

        write_pod(v);
        return *this;
    }

    template <typename T>
        requires std::is_arithmetic_v<T>
    LinearBuffer& operator>>(T& out) noexcept {
        if (fail_) {
            return *this;
        }

        if (size() < sizeof(T)) {
            fail_ = true;
            return *this;
        }

        read_pod(out);
        return *this;
    }

public:
    std::size_t capacity() const noexcept { return capacity_; }
    std::size_t size() const noexcept { return write_pos_ - read_pos_; }
    std::size_t available() const noexcept { return capacity_ - write_pos_; }
    bool empty() const noexcept { return read_pos_ == write_pos_; }

public:
    const std::byte* read_ptr() const noexcept {
        return buf_.get() + read_pos_;
    }
    std::byte* write_ptr() noexcept { return buf_.get() + write_pos_; }

    void set_fail() noexcept { fail_ = true; }

public:
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
        fail_ = false;
    }

public:
    bool peek(std::byte* dst, std::size_t n) const noexcept {
        if (size() < n) {
            return false;
        }

        std::memcpy(dst, buf_.get() + read_pos_, n);
        return true;
    }
    bool read(std::byte* dst, std::size_t n) noexcept {
        if (size() < n) {
            return false;
        }

        std::memcpy(dst, buf_.get() + read_pos_, n);
        read_pos_ += n;
        return true;
    }
    bool write(const std::byte* src, std::size_t n) noexcept {
        if (available() < n) {
            return false;
        }

        std::memcpy(buf_.get() + write_pos_, src, n);
        write_pos_ += n;
        return true;
    }

private:
    template <typename T>
    void read_pod(T& out) noexcept {
        std::memcpy(&out, buf_.get() + read_pos_, sizeof(T));
        read_pos_ += sizeof(T);
    }
    template <typename T>
    void write_pod(const T& v) noexcept {
        std::memcpy(buf_.get() + write_pos_, &v, sizeof(T));
        write_pos_ += sizeof(T);
    }

private:
    std::unique_ptr<std::byte[]> buf_;
    std::size_t capacity_;
    std::size_t read_pos_{0};
    std::size_t write_pos_{0};
    bool fail_{false};
};

} // namespace linbuf::v01
