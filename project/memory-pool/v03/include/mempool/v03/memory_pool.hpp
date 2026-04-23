#pragma once

#include <array>
#include <bit>
#include <new>
#include <utility>
#include <vector>

#include <cassert>
#include <cstddef>

#include <sys/mman.h>

namespace mempool::v03 {

class MemoryPool {
public:
    static constexpr std::size_t kMinSize = 16;
    static constexpr std::size_t kMaxSize = 1024;
    static constexpr std::size_t kSlabSize = 64 * 1024; // 64 KiB
    static constexpr std::size_t kClassCount = 7; // 16,32,64,128,256,512,1024

public:
    MemoryPool() noexcept {
        std::size_t size = kMinSize;

        for (auto& cls : classes_) {
            cls.slot_size = size;
            cls.free_head = nullptr;

            size <<= 1;
        }
    }
    ~MemoryPool() {
        for (const auto& cls : classes_) {
            for (const auto& slab : cls.slabs) {
                munmap(slab.buf, kSlabSize);
            }
        }
    }

    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;
    MemoryPool(MemoryPool&& other) noexcept
        : classes_{std::move(other.classes_)} {
        for (auto& cls : other.classes_) {
            cls.free_head = nullptr;
        }
    }
    MemoryPool& operator=(MemoryPool&& other) noexcept {
        if (this != &other) {
            for (const auto& cls : classes_) {
                for (const auto& slab : cls.slabs) {
                    munmap(slab.buf, kSlabSize);
                }
            }
            classes_ = std::move(other.classes_);
            for (auto& cls : other.classes_) {
                cls.free_head = nullptr;
            }
        }
        return *this;
    }

public: // 상태
    std::size_t total_capacity() const noexcept {
        std::size_t slab_count{0};

        for (const auto& cls : classes_) {
            slab_count += cls.slabs.size();
        }

        return slab_count * kSlabSize;
    }

public: // 상태 변경
    void* allocate(std::size_t n) {
        if (n == 0 || n > kMaxSize) {
            throw std::bad_alloc{};
        }

        // n보다 크거나 같은 수들 중 가장 작은 2의 거듭제곱 인덱스 구하기
        std::size_t idx = size_class_of(n);

        auto& cls = classes_[idx];

        if (cls.free_head == nullptr) {
            auto ptr = mmap(
                nullptr, kSlabSize, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0
            );
            if (ptr == MAP_FAILED) {
                throw std::bad_alloc{};
            }

            try {
                cls.slabs.emplace_back(reinterpret_cast<std::byte*>(ptr));
            } catch (...) {
                munmap(ptr, kSlabSize);
                throw;
            }

            // slab 내 slot 초기화 (intrusive free list로 연결)
            const std::size_t slot_count = kSlabSize / cls.slot_size;
            std::byte* base = cls.slabs.back().buf;

            for (std::size_t i = 0; i < slot_count; ++i) {
                std::byte* next = (i + 1 < slot_count)
                                      ? base + (i + 1) * cls.slot_size
                                      : nullptr;
                *reinterpret_cast<void**>(base + i * cls.slot_size) = next;
            }

            cls.free_head = base;
        }

        void* slot = cls.free_head;
        cls.free_head = *reinterpret_cast<void**>(slot);

        return slot;
    }

    // alloc 시 요청한 n을 그대로. size class 매핑 일관성 사용자 책임.
    void deallocate(void* p, std::size_t n) noexcept {
        assert(n > 0 && n <= kMaxSize);

        std::size_t idx = size_class_of(n);

        auto& cls = classes_[idx];

        // p가 slot_size 정렬되어 있는지 확인
        assert((reinterpret_cast<std::size_t>(p) & (cls.slot_size - 1)) == 0);

        // intrusive push: p의 첫 8B에 옛 head 저장 후 head 갱신
        *reinterpret_cast<void**>(p) = cls.free_head;
        cls.free_head = p;
    }

    // 모든 객체 free 상태로
    void reset() noexcept {
        for (auto& cls : classes_) {
            for (const auto& slab : cls.slabs) {
                munmap(slab.buf, kSlabSize);
            }
            cls.slabs.clear();
            cls.free_head = nullptr;
        }
    }

private:
    // size class 인덱스 계산 함수
    static constexpr std::size_t size_class_of(std::size_t n) {
        return n <= kMinSize
                   ? 0
                   : static_cast<std::size_t>(std::bit_width(n - 1) - 4);
    }

private:
    struct Slab {
        std::byte* buf;
    };
    struct SizeClass {
        std::size_t slot_size;
        void* free_head;
        std::vector<Slab> slabs;
    };

private:
    std::array<SizeClass, kClassCount> classes_;
};

} // namespace mempool::v03
