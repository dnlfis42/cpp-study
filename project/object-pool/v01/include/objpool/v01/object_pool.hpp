#pragma once

#include <numeric>
#include <vector>

#include <cassert>
#include <cstddef>

namespace objpool::v01 {

template <typename T>
class ObjectPool {
public:
    explicit ObjectPool(std::size_t capacity)
        : storage_(capacity), free_list_(capacity) {
        std::iota(free_list_.begin(), free_list_.end(), std::size_t{0});
    }
    ~ObjectPool() = default;

    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;
    ObjectPool(ObjectPool&&) noexcept = default;
    ObjectPool& operator=(ObjectPool&&) noexcept = default;

public: // 상태
    std::size_t capacity() const noexcept { return storage_.size(); }
    std::size_t available() const noexcept { return free_list_.size(); }
    std::size_t in_use() const noexcept { return capacity() - available(); }

public: // 획득/해제
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

private: // 멤버
    std::vector<T> storage_;
    std::vector<std::size_t> free_list_;
};

} // namespace objpool::v01
