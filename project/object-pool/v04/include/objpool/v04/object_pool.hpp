#pragma once

#include <memory>
#include <vector>

#include <cassert>
#include <cstddef>

namespace objpool::v04 {

// v03와 storage 구조 동일 (Node + head_free_ 링크드 리스트).
// 변경점: Raw API(acquire/release)를 private로 내림 — Handle만 공개.
// → 메모리 안전: release 누락 불가능, cross-pool 반환 불가능.
template <typename T>
class ObjectPool {
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
    explicit ObjectPool(std::size_t capacity)
        : storage_(capacity), head_free_{capacity == 0 ? SENTINEL : 0},
          available_{capacity} {
        for (std::size_t i = 0; i + 1 < capacity; ++i) {
            storage_[i].next = i + 1;
        }
        if (capacity > 0) {
            storage_[capacity - 1].next = SENTINEL;
        }
    }
    ~ObjectPool() = default;

    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;
    ObjectPool(ObjectPool&&) noexcept = default;
    ObjectPool& operator=(ObjectPool&&) noexcept = default;

public: // 상태
    std::size_t capacity() const noexcept { return storage_.size(); }
    std::size_t available() const noexcept { return available_; }
    std::size_t in_use() const noexcept { return capacity() - available_; }

public: // 획득 (유일한 경로)
    // 실패 시 빈 Handle 반환.
    Handle acquire() noexcept { return Handle{acquire_raw(), Deleter{this}}; }

private: // 중첩 타입
    struct Node {
        T data;
        std::size_t next;
    };

    static constexpr std::size_t SENTINEL = std::size_t(-1);

private: // Raw — 외부 노출 금지. Deleter만 호출 가능.
    T* acquire_raw() noexcept {
        if (head_free_ == SENTINEL) {
            return nullptr;
        }
        const std::size_t idx = head_free_;
        head_free_ = storage_[idx].next;
        --available_;
        return &storage_[idx].data;
    }

    void release(T* obj) noexcept {
        assert(obj != nullptr);
        Node* node = reinterpret_cast<Node*>(obj);
        const std::size_t idx =
            static_cast<std::size_t>(node - storage_.data());
        assert(idx < storage_.size());
        assert(available_ < storage_.size());

        storage_[idx].next = head_free_;
        head_free_ = idx;
        ++available_;
    }

private: // 멤버
    std::vector<Node> storage_;
    std::size_t head_free_;
    std::size_t available_;
};

} // namespace objpool::v04
