#pragma once

#include <new>

#include <cassert>
#include <cstddef>

#include <sys/mman.h>

namespace mempool::v01 {

class MemoryPool {
public:
    explicit MemoryPool(std::size_t capacity)
        : buf_{}, capacity_{capacity}, pos_{} {
        auto ptr = mmap(
            nullptr, capacity_, PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0
        );
        if (ptr == MAP_FAILED) {
            throw std::bad_alloc{};
        }

        buf_ = reinterpret_cast<std::byte*>(ptr);
    }
    ~MemoryPool() {
        if (buf_) {
            munmap(buf_, capacity_);
        }
    }

    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;
    MemoryPool(MemoryPool&& other) noexcept
        : buf_{other.buf_}, capacity_{other.capacity_}, pos_{other.pos_} {
        other.buf_ = nullptr;
        other.capacity_ = 0;
        other.pos_ = 0;
    }
    MemoryPool& operator=(MemoryPool&& other) noexcept {
        if (this != &other) {
            if (buf_) {
                munmap(buf_, capacity_);
            }

            buf_ = other.buf_;
            capacity_ = other.capacity_;
            pos_ = other.pos_;

            other.buf_ = nullptr;
            other.capacity_ = 0;
            other.pos_ = 0;
        }

        return *this;
    }

public: // 상태
    std::size_t capacity() const noexcept { return capacity_; }
    std::size_t available() const noexcept { return capacity_ - pos_; }
    std::size_t in_use() const noexcept { return pos_; }

public: // 상태 변경
    void* allocate(std::size_t n) {
        return allocate(n, alignof(std::max_align_t));
    }
    void* allocate(std::size_t n, std::size_t align) {
        // 2의 거듭제곱 검증
        assert(align != 0 && (align & (align - 1)) == 0);

        std::size_t aligned_pos = pos_;
        aligned_pos = (aligned_pos + (align - 1)) & ~(align - 1);

        if (aligned_pos > capacity_ || n > capacity_ - aligned_pos) {
            throw std::bad_alloc{};
        }

        pos_ = aligned_pos + n;

        return buf_ + aligned_pos;
    }
    void reset() noexcept { pos_ = 0; }

private:
private:
    std::byte* buf_;
    std::size_t capacity_;
    std::size_t pos_;
};

} // namespace mempool::v01
