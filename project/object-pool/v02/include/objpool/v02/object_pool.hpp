#pragma once

#include <memory>
#include <numeric>
#include <vector>

#include <cassert>
#include <cstddef>

namespace objpool::v02 {

template <typename T>
class ObjectPool {
public:
    class Deleter {
    public:
        Deleter() noexcept : pool_{nullptr} {}
        explicit Deleter(ObjectPool* pool) noexcept : pool_{pool} {}

    public:
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
    explicit ObjectPool(std::size_t capacity)
        : storage_(capacity), free_list_(capacity) {
        std::iota(free_list_.begin(), free_list_.end(), std::size_t{0});
    }
    ~ObjectPool() = default;

    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;
    ObjectPool(ObjectPool&&) = delete;
    ObjectPool& operator=(ObjectPool&&) = delete;

public:
    std::size_t capacity() const noexcept { return storage_.size(); }
    std::size_t available() const noexcept { return free_list_.size(); }
    std::size_t in_use() const noexcept { return capacity() - available(); }

public:
    [[nodiscard]]
    T* acquire() noexcept {
        if (free_list_.empty()) {
            return nullptr;
        }

        const std::size_t idx = free_list_.back();
        free_list_.pop_back();

        return &storage_[idx];
    }

    void release(T* obj) noexcept {
        assert(obj != nullptr);

        const std::size_t idx = static_cast<std::size_t>(obj - storage_.data());

        assert(idx < storage_.size());
        assert(free_list_.size() < storage_.size());

        free_list_.push_back(idx);
    }

public:
    [[nodiscard]]
    Handle acquire_unique() noexcept {
        return Handle{acquire(), Deleter{this}};
    }

private:
    std::vector<T> storage_;
    std::vector<std::size_t> free_list_;
};

} // namespace objpool::v02
