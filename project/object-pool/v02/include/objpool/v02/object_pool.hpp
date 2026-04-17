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
    // unique_ptr와 함께 쓰는 Deleter — 호출 시 pool->release 호출.
    // 기본 생성자는 "빈 Deleter" (pool == nullptr), 빈 Handle(acquire 실패)에
    // 대응.
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
    explicit ObjectPool(std::size_t capacity)
        : storage_(capacity), free_list_(capacity) {
        std::iota(free_list_.begin(), free_list_.end(), std::size_t{0});
    }
    ~ObjectPool() = default;

    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;
    // 주의: Handle이 outstanding인 동안 move하면 Deleter의 pool_이 dangling.
    // 사용자 책임.
    ObjectPool(ObjectPool&&) noexcept = default;
    ObjectPool& operator=(ObjectPool&&) noexcept = default;

public: // 상태
    std::size_t capacity() const noexcept { return storage_.size(); }
    std::size_t available() const noexcept { return free_list_.size(); }
    std::size_t in_use() const noexcept { return capacity() - available(); }

public: // 획득/해제 (Raw API — v01과 동일)
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

public: // 획득 (RAII Handle)
    // 실패 시 빈 Handle 반환 (Handle은 bool 변환 시 false).
    Handle acquire_unique() noexcept {
        return Handle{acquire(), Deleter{this}};
    }

private: // 멤버
    std::vector<T> storage_;
    std::vector<std::size_t> free_list_;
};

} // namespace objpool::v02
