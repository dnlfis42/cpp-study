#pragma once

#include <memory>
#include <vector>

#include <cassert>
#include <cstddef>

namespace objpool::v04 {

template <typename T>
class ObjectPool {
private:
    struct Node {
        T data;
        Node* next;
    };

public:
    class Deleter {
    public:
        Deleter() noexcept : pool_{nullptr}, node_{nullptr} {}
        Deleter(ObjectPool* pool, Node* node) noexcept
            : pool_{pool}, node_{node} {}

    public:
        void operator()(T*) const noexcept {
            assert(pool_ != nullptr);
            pool_->release(node_);
        }

    private:
        ObjectPool* pool_;
        Node* node_;
    };

    using Handle = std::unique_ptr<T, Deleter>;

public:
    explicit ObjectPool(std::size_t chunk_size = 64)
        : head_free_{nullptr}, chunk_size_{chunk_size}, capacity_{0},
          available_{0} {
        assert(chunk_size > 0);
    }

    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;
    ObjectPool(ObjectPool&&) = delete;
    ObjectPool& operator=(ObjectPool&&) = delete;

public:
    std::size_t capacity() const noexcept { return capacity_; }
    std::size_t available() const noexcept { return available_; }
    std::size_t in_use() const noexcept { return capacity_ - available_; }
    std::size_t chunk_size() const noexcept { return chunk_size_; }

public:
    [[nodiscard]]
    Handle acquire() {
        if (head_free_ == nullptr) {
            grow();
        }

        Node* node = head_free_;
        head_free_ = node->next;
        --available_;

        return Handle{&node->data, Deleter{this, node}};
    }

private:
    void release(Node* node) noexcept {
        assert(node != nullptr);

        node->next = head_free_;
        head_free_ = node;
        ++available_;
    }

    void grow() {
        auto new_chunk = std::make_unique<Node[]>(chunk_size_);

        for (std::size_t i = 0; i + 1 < chunk_size_; ++i) {
            new_chunk[i].next = &new_chunk[i + 1];
        }
        new_chunk[chunk_size_ - 1].next = head_free_;

        head_free_ = &new_chunk[0];
        capacity_ += chunk_size_;
        available_ += chunk_size_;

        chunks_.push_back(std::move(new_chunk));
    }

private:
    std::vector<std::unique_ptr<Node[]>> chunks_;
    Node* head_free_;
    std::size_t chunk_size_;
    std::size_t capacity_;
    std::size_t available_;
};

} // namespace objpool::v04
