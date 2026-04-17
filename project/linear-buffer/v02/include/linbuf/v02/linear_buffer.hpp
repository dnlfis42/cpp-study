#pragma once

#include <memory>
#include <span>
#include <stdexcept>

#include <cstddef>
#include <cstring>

namespace linbuf::v02 {

class LinearBuffer {
public:
    explicit LinearBuffer(std::size_t capacity)
        : buf_{new std::byte[capacity]}, capacity_{capacity} {}
    ~LinearBuffer() = default;

    LinearBuffer(const LinearBuffer&) = delete;
    LinearBuffer& operator=(const LinearBuffer&) = delete;
    LinearBuffer(LinearBuffer&&) noexcept = default;
    LinearBuffer& operator=(LinearBuffer&&) noexcept = default;

public: // 상태
    std::size_t capacity() const noexcept { return capacity_; }
    std::size_t size() const noexcept { return write_pos_ - read_pos_; }
    std::size_t available() const noexcept { return capacity_ - write_pos_; }
    bool empty() const noexcept { return read_pos_ == write_pos_; }

public: // 상태 변경
    void clear() noexcept {
        read_pos_ = 0;
        write_pos_ = 0;
    }

public: // 직접 접근 (zero-copy)
    const std::byte* read_ptr() const noexcept {
        return buf_.get() + read_pos_;
    }
    std::byte* write_ptr() noexcept { return buf_.get() + write_pos_; }

    // 현재 읽을 수 있는 영역 전체를 span으로. read_pos 이동 없음.
    std::span<const std::byte> read_span() const noexcept {
        return {buf_.get() + read_pos_, size()};
    }

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

public: // raw 바이트
    bool read(std::byte* dst, std::size_t n) noexcept {
        if (size() < n) {
            return false;
        }
        std::memcpy(dst, buf_.get() + read_pos_, n);
        read_pos_ += n;
        return true;
    }

    // zero-copy read: n바이트 span 반환 + read_pos 전진.
    // 부족하면 빈 span 반환, 상태 변경 없음.
    std::span<const std::byte> read(std::size_t n) noexcept {
        if (size() < n) {
            return {};
        }
        const auto pos = read_pos_;
        read_pos_ += n;
        return {buf_.get() + pos, n};
    }

    bool write(const std::byte* src, std::size_t n) noexcept {
        if (available() < n) {
            return false;
        }
        std::memcpy(buf_.get() + write_pos_, src, n);
        write_pos_ += n;
        return true;
    }

    bool peek(std::byte* dst, std::size_t n) const noexcept {
        if (size() < n) {
            return false;
        }
        std::memcpy(dst, buf_.get() + read_pos_, n);
        return true;
    }

    // zero-copy peek: n바이트 span 반환, read_pos 변경 없음.
    std::span<const std::byte> peek(std::size_t n) const noexcept {
        if (size() < n) {
            return {};
        }
        return {buf_.get() + read_pos_, n};
    }

public: // primitive 직렬화
    LinearBuffer& operator<<(bool v) {
        write_pod(v);
        return *this;
    }
    LinearBuffer& operator<<(char v) {
        write_pod(v);
        return *this;
    }
    LinearBuffer& operator<<(signed char v) {
        write_pod(v);
        return *this;
    }
    LinearBuffer& operator<<(unsigned char v) {
        write_pod(v);
        return *this;
    }
    LinearBuffer& operator<<(char8_t v) {
        write_pod(v);
        return *this;
    }
    LinearBuffer& operator<<(char16_t v) {
        write_pod(v);
        return *this;
    }
    LinearBuffer& operator<<(char32_t v) {
        write_pod(v);
        return *this;
    }
    LinearBuffer& operator<<(wchar_t v) {
        write_pod(v);
        return *this;
    }
    LinearBuffer& operator<<(short v) {
        write_pod(v);
        return *this;
    }
    LinearBuffer& operator<<(unsigned short v) {
        write_pod(v);
        return *this;
    }
    LinearBuffer& operator<<(int v) {
        write_pod(v);
        return *this;
    }
    LinearBuffer& operator<<(unsigned int v) {
        write_pod(v);
        return *this;
    }
    LinearBuffer& operator<<(long v) {
        write_pod(v);
        return *this;
    }
    LinearBuffer& operator<<(unsigned long v) {
        write_pod(v);
        return *this;
    }
    LinearBuffer& operator<<(long long v) {
        write_pod(v);
        return *this;
    }
    LinearBuffer& operator<<(unsigned long long v) {
        write_pod(v);
        return *this;
    }
    LinearBuffer& operator<<(float v) {
        write_pod(v);
        return *this;
    }
    LinearBuffer& operator<<(double v) {
        write_pod(v);
        return *this;
    }
    LinearBuffer& operator<<(long double v) {
        write_pod(v);
        return *this;
    }

    LinearBuffer& operator>>(bool& out) {
        read_pod(out);
        return *this;
    }
    LinearBuffer& operator>>(char& out) {
        read_pod(out);
        return *this;
    }
    LinearBuffer& operator>>(signed char& out) {
        read_pod(out);
        return *this;
    }
    LinearBuffer& operator>>(unsigned char& out) {
        read_pod(out);
        return *this;
    }
    LinearBuffer& operator>>(char8_t& out) {
        read_pod(out);
        return *this;
    }
    LinearBuffer& operator>>(char16_t& out) {
        read_pod(out);
        return *this;
    }
    LinearBuffer& operator>>(char32_t& out) {
        read_pod(out);
        return *this;
    }
    LinearBuffer& operator>>(wchar_t& out) {
        read_pod(out);
        return *this;
    }
    LinearBuffer& operator>>(short& out) {
        read_pod(out);
        return *this;
    }
    LinearBuffer& operator>>(unsigned short& out) {
        read_pod(out);
        return *this;
    }
    LinearBuffer& operator>>(int& out) {
        read_pod(out);
        return *this;
    }
    LinearBuffer& operator>>(unsigned int& out) {
        read_pod(out);
        return *this;
    }
    LinearBuffer& operator>>(long& out) {
        read_pod(out);
        return *this;
    }
    LinearBuffer& operator>>(unsigned long& out) {
        read_pod(out);
        return *this;
    }
    LinearBuffer& operator>>(long long& out) {
        read_pod(out);
        return *this;
    }
    LinearBuffer& operator>>(unsigned long long& out) {
        read_pod(out);
        return *this;
    }
    LinearBuffer& operator>>(float& out) {
        read_pod(out);
        return *this;
    }
    LinearBuffer& operator>>(double& out) {
        read_pod(out);
        return *this;
    }
    LinearBuffer& operator>>(long double& out) {
        read_pod(out);
        return *this;
    }

private:
    template <typename T>
    void read_pod(T& out) {
        if (size() < sizeof(T)) {
            throw std::runtime_error("LinearBuffer: insufficient data");
        }
        std::memcpy(&out, buf_.get() + read_pos_, sizeof(T));
        read_pos_ += sizeof(T);
    }
    template <typename T>
    void write_pod(const T& v) {
        if (available() < sizeof(T)) {
            throw std::runtime_error("LinearBuffer: insufficient space");
        }
        std::memcpy(buf_.get() + write_pos_, &v, sizeof(T));
        write_pos_ += sizeof(T);
    }

private: // 멤버
    std::unique_ptr<std::byte[]> buf_;
    std::size_t capacity_;
    std::size_t read_pos_{0};
    std::size_t write_pos_{0};
};

} // namespace linbuf::v02
