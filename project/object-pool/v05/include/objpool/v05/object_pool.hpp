#pragma once

#include <memory>
#include <vector>

#include <cassert>
#include <cstddef>

namespace objpool::v05 {

// 가변 크기 풀.
// - Non-intrusive Node { T data; Node* next; } — next는 포인터
// - 청크 기반 storage — 고갈 시 새 청크 할당, 기존 포인터 안정성 보장
// - Handle (Raii, unique_ptr) 만 공개
// - lazy 초기화 — 생성자에선 청크 할당 없음, 첫 acquire()에서 첫 청크 생성
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
    explicit ObjectPool(std::size_t chunk_size = 64)
        : head_free_{nullptr}, chunk_size_{chunk_size}, capacity_{0},
          available_{0} {
        assert(chunk_size > 0);
    }
    ~ObjectPool() = default;

    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;
    ObjectPool(ObjectPool&&) noexcept = default;
    ObjectPool& operator=(ObjectPool&&) noexcept = default;

public: // 상태
    std::size_t capacity() const noexcept { return capacity_; }
    std::size_t available() const noexcept { return available_; }
    std::size_t in_use() const noexcept { return capacity_ - available_; }
    std::size_t chunk_size() const noexcept { return chunk_size_; }

public: // 획득 (유일한 경로, 고갈 시 자동 성장)
    // bad_alloc throw 가능 (grow 시).
    Handle acquire() {
        if (head_free_ == nullptr) {
            grow();
        }
        Node* node = head_free_;
        head_free_ = node->next;
        --available_;
        return Handle{&node->data, Deleter{this}};
    }

private: // 중첩 타입
    struct Node {
        T data;
        Node* next;
    };

private: // Deleter만 호출
    void release(T* obj) noexcept {
        assert(obj != nullptr);
        // T*를 Node*로 역산 (data가 Node의 첫 멤버라 offset 0)
        Node* node = reinterpret_cast<Node*>(obj);
        node->next = head_free_;
        head_free_ = node;
        ++available_;
    }

    void grow() {
        auto new_chunk = std::make_unique<Node[]>(chunk_size_);
        // 청크 내부 연결: 0 → 1 → ... → chunk_size-1
        for (std::size_t i = 0; i + 1 < chunk_size_; ++i) {
            new_chunk[i].next = &new_chunk[i + 1];
        }
        // 마지막 Node의 next는 기존 head_free_ (nullptr 또는 이전 free list)
        new_chunk[chunk_size_ - 1].next = head_free_;
        // 새 head는 청크의 첫 Node
        head_free_ = &new_chunk[0];

        capacity_ += chunk_size_;
        available_ += chunk_size_;
        chunks_.push_back(std::move(new_chunk));
    }

private: // 멤버
    std::vector<std::unique_ptr<Node[]>> chunks_;
    Node* head_free_;
    std::size_t chunk_size_;
    std::size_t capacity_;
    std::size_t available_;
};

} // namespace objpool::v05
