#pragma once

#include <new>
#include <utility>
#include <vector>

#include <cassert>
#include <cstddef>

#include <sys/mman.h>

namespace mempool::v02 {

struct Chunk {
    std::byte* buf;
    std::size_t capacity;
    std::size_t pos;
};

class MemoryPool {
public:
    explicit MemoryPool(std::size_t chunk_size) : chunk_size_{chunk_size} {
        auto ptr = mmap(
            nullptr, chunk_size_, PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0
        );
        if (ptr == MAP_FAILED) {
            throw std::bad_alloc{};
        }

        try {
            small_chunks_.emplace_back(
                reinterpret_cast<std::byte*>(ptr), chunk_size_, 0
            );
        } catch (...) {
            munmap(ptr, chunk_size_);
            throw;
        }
    }
    ~MemoryPool() {
        for (auto& chunk : small_chunks_) {
            munmap(chunk.buf, chunk.capacity);
        }
        for (auto& chunk : large_chunks_) {
            munmap(chunk.buf, chunk.capacity);
        }
    }

    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;
    MemoryPool(MemoryPool&& other) noexcept
        : chunk_size_{other.chunk_size_},
          small_chunks_(std::move(other.small_chunks_)),
          large_chunks_(std::move(other.large_chunks_)),
          current_{other.current_} {

        other.chunk_size_ = 0;
        other.current_ = 0;
    }
    MemoryPool& operator=(MemoryPool&& other) noexcept {
        if (this != &other) {
            for (auto& chunk : small_chunks_) {
                munmap(chunk.buf, chunk.capacity);
            }
            for (auto& chunk : large_chunks_) {
                munmap(chunk.buf, chunk.capacity);
            }

            chunk_size_ = other.chunk_size_;
            small_chunks_ = std::move(other.small_chunks_);
            large_chunks_ = std::move(other.large_chunks_);
            current_ = other.current_;

            other.chunk_size_ = 0;
            other.current_ = 0;
        }

        return *this;
    }

public:
    std::size_t chunk_size() const noexcept { return chunk_size_; }
    std::size_t large_threshold() const noexcept { return chunk_size_ >> 1; }
    std::size_t total_capacity() const noexcept {
        std::size_t total{};

        for (const auto& chunk : small_chunks_) {
            total += chunk.capacity;
        }
        for (const auto& chunk : large_chunks_) {
            total += chunk.capacity;
        }

        return total;
    }
    std::size_t total_in_use() const noexcept {
        std::size_t total{};

        for (const auto& chunk : small_chunks_) {
            total += chunk.pos;
        }
        for (const auto& chunk : large_chunks_) {
            total += chunk.pos;
        }

        return total;
    }

public: // 상태 변경
    void* allocate(std::size_t n) {
        return allocate(n, alignof(std::max_align_t));
    }
    void* allocate(std::size_t n, std::size_t align) {
        // 2의 거듭제곱 검증
        assert(align != 0 && (align & (align - 1)) == 0);

        // threshold 넘는지 확인
        if (n > (chunk_size_ >> 1)) {
            auto large_chunk_size = (n + 4095) & ~std::size_t{4095};

            // align은 4KB 이하라고 가정
            auto ptr = mmap(
                nullptr, large_chunk_size, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0
            );
            if (ptr == MAP_FAILED) {
                throw std::bad_alloc{};
            }

            try {
                large_chunks_.emplace_back(
                    reinterpret_cast<std::byte*>(ptr), large_chunk_size, n
                );
            } catch (...) {
                munmap(ptr, large_chunk_size);
                throw;
            }

            return large_chunks_.back().buf;
        }

        auto& current_chunk = small_chunks_[current_];

        std::size_t aligned_pos = current_chunk.pos;
        aligned_pos = (aligned_pos + (align - 1)) & ~(align - 1);

        if (aligned_pos > current_chunk.capacity ||
            n > current_chunk.capacity - aligned_pos) {
            if (current_ + 1 == small_chunks_.size()) {
                // 새 small chunk 추가
                auto ptr = mmap(
                    nullptr, chunk_size_, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0
                );

                if (ptr == MAP_FAILED) {
                    throw std::bad_alloc{};
                }

                try {
                    small_chunks_.emplace_back(
                        reinterpret_cast<std::byte*>(ptr), chunk_size_, 0
                    );
                } catch (...) {
                    munmap(ptr, chunk_size_);
                    throw;
                }
            }

            ++current_;

            small_chunks_[current_].pos = n;

            return small_chunks_[current_].buf;
        }

        current_chunk.pos = aligned_pos + n;

        return current_chunk.buf + aligned_pos;
    }
    void reset() noexcept {
        for (auto& chunk : small_chunks_) {
            chunk.pos = 0;
        }
        current_ = 0;

        for (auto& chunk : large_chunks_) {
            munmap(chunk.buf, chunk.capacity);
        }
        large_chunks_.clear();
    }

private:
    std::size_t chunk_size_;
    std::vector<Chunk> small_chunks_;
    std::vector<Chunk> large_chunks_;
    std::size_t current_{0};
};

} // namespace mempool::v02
