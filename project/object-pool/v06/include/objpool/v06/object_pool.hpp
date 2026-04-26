#pragma once

#include "mempool/v03/memory_pool.hpp"

#include <memory>
#include <utility>

#include <cassert>
#include <cstddef>

namespace objpool::v06 {

// MemoryPool v03 slab을 백엔드로 사용하는 object pool.
// - acquire: raw slot 획득 → placement new (생성자 호출)
// - release: explicit 소멸자 → slot 반납
// - T 크기 제한: sizeof(T) <= 1024 (mempool v03 kMaxSize)
template <typename T>
class ObjectPool {
    static_assert(
        sizeof(T) <= mempool::v03::MemoryPool::kMaxSize,
        "T too large for MemoryPool v03 (max 1024 bytes)"
    );
    static_assert(
        alignof(T) <= sizeof(T), "T alignment exceeds slot alignment guarantee"
    );

public:
    class Deleter {
    public:
        Deleter() noexcept : pool_{nullptr} {}
        explicit Deleter(ObjectPool* pool) noexcept : pool_{pool} {}

        void operator()(T* obj) const noexcept {
            if (pool_ != nullptr && obj != nullptr) {
                pool_->release(obj);
            }
        }

    private:
        ObjectPool* pool_;
    };

    using Handle = std::unique_ptr<T, Deleter>;

public:
    ObjectPool() = default;
    ~ObjectPool() = default;

    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;
    ObjectPool(ObjectPool&&) noexcept = default;
    ObjectPool& operator=(ObjectPool&&) noexcept = default;

public: // 상태
    std::size_t in_use() const noexcept { return in_use_; }
    std::size_t total_capacity() const noexcept {
        return pool_.total_capacity();
    }

public: // 획득
    template <typename... Args>
    Handle acquire(Args&&... args) {
        void* raw = pool_.allocate(sizeof(T));
        try {
            T* obj = new (raw) T{std::forward<Args>(args)...};
            ++in_use_;
            return Handle{obj, Deleter{this}};
        } catch (...) {
            pool_.deallocate(raw, sizeof(T));
            throw;
        }
    }

private:
    void release(T* obj) noexcept {
        assert(obj != nullptr);
        obj->~T();
        pool_.deallocate(obj, sizeof(T));
        --in_use_;
    }

private:
    mempool::v03::MemoryPool pool_{};
    std::size_t in_use_{0};
};

} // namespace objpool::v06
