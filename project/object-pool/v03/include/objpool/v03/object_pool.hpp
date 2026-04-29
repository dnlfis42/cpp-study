#pragma once

#include <memory>
#include <vector>

#include <cassert>
#include <cstddef>

namespace objpool::v03 {

template <typename T>
class ObjectPool {
private:
    static constexpr std::size_t sentinel = std::size_t(-1);

    struct Node {
        T data;
        std::size_t next;
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
    explicit ObjectPool(std::size_t capacity)
        : storage_(capacity), head_free_{capacity == 0 ? sentinel : 0},
          available_{capacity} {
        for (std::size_t i = 0; i + 1 < capacity; ++i) {
            storage_[i].next = i + 1;
        }
        if (capacity > 0) {
            storage_[capacity - 1].next = sentinel;
        }
    }

    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;
    ObjectPool(ObjectPool&&) = delete;
    ObjectPool& operator=(ObjectPool&&) = delete;

public:
    std::size_t capacity() const noexcept { return storage_.size(); }
    std::size_t available() const noexcept { return available_; }
    std::size_t in_use() const noexcept { return capacity() - available_; }

    [[nodiscard]]
    Handle acquire() noexcept {
        if (head_free_ == sentinel) {
            return Handle{};
        }

        const std::size_t idx = head_free_;
        head_free_ = storage_[idx].next;
        --available_;

        return Handle{&storage_[idx].data, Deleter{this, &storage_[idx]}};
    }

private:
    void release(Node* node) noexcept {
        assert(node != nullptr);

        const std::size_t idx =
            static_cast<std::size_t>(node - storage_.data());

        assert(idx < storage_.size());
        assert(available_ < storage_.size());

        node->next = head_free_;
        head_free_ = idx;
        ++available_;
    }

private:
    std::vector<Node> storage_;
    std::size_t head_free_;
    std::size_t available_;
};

} // namespace objpool::v03
