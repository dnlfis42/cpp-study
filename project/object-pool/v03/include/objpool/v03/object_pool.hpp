#pragma once

#include <memory>
#include <vector>

#include <cassert>
#include <cstddef>

namespace objpool::v03 {

template <typename T>
class ObjectPool {
public:
    // RAII Handle (v02와 동일)
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
        // 빈 슬롯 연결 리스트: 0 → 1 → 2 → ... → N-1 → SENTINEL
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

public: // 획득/해제 (Raw API)
    T* acquire() noexcept {
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
        // T*를 Node*로 역산 (data가 Node의 첫 멤버라 offset 0)
        Node* node = reinterpret_cast<Node*>(obj);
        const std::size_t idx =
            static_cast<std::size_t>(node - storage_.data());
        assert(idx < storage_.size());
        assert(available_ < storage_.size()); // 이중 release 방지

        storage_[idx].next = head_free_;
        head_free_ = idx;
        ++available_;
    }

public: // 획득 (RAII Handle)
    Handle acquire_unique() noexcept {
        return Handle{acquire(), Deleter{this}};
    }

private: // 중첩 타입
    struct Node {
        T data;
        std::size_t next; ///< 다음 빈 Node 인덱스 (빈 슬롯일 때만 유효),
                          ///< SENTINEL이면 마지막
    };

    static constexpr std::size_t SENTINEL = std::size_t(-1);

private: // 멤버
    std::vector<Node> storage_;
    std::size_t head_free_;
    std::size_t available_;
};

} // namespace objpool::v03
