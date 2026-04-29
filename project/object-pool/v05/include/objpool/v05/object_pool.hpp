#pragma once

#include "mempool/v03/memory_pool.hpp"

#include <memory>
#include <utility>

#include <cassert>
#include <cstddef>

namespace objpool::v05 {

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

    public:
        void operator()(T* obj) const noexcept {
            assert(pool_ != nullptr);
            pool_->release(obj);
        }

    private:
        ObjectPool* pool_;
    };

    using Handle = std::unique_ptr<T, Deleter>;

public:
    ObjectPool() = default;

    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;
    ObjectPool(ObjectPool&&) = delete;
    ObjectPool& operator=(ObjectPool&&) = delete;

public:
    std::size_t total_capacity() const noexcept {
        return pool_.total_capacity();
    }
    std::size_t in_use() const noexcept { return in_use_; }

public:
    template <typename... Args>
    [[nodiscard]]
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

} // namespace objpool::v05
