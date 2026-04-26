#pragma once

#include <atomic>
#include <memory>
#include <optional>

#include <cstddef>
#include <utility>

namespace spscq::v04 {

template <typename T, std::size_t N>
class SpscQueue {
    static_assert((N & (N - 1)) == 0, "N must be power of 2");

public:
    SpscQueue() : buf_{std::make_unique<T[]>(N)}, head_{0}, tail_{0} {}
    ~SpscQueue() = default;

    SpscQueue(const SpscQueue&) = delete;
    SpscQueue& operator=(const SpscQueue&) = delete;
    SpscQueue(SpscQueue&&) = delete;
    SpscQueue& operator=(SpscQueue&&) = delete;

public:
    std::size_t capacity() const noexcept { return N - 1; }
    std::size_t size() const noexcept { return (N + tail_ - head_) & (N - 1); }
    bool empty() const noexcept { return head_ == tail_; }
    bool full() const noexcept { return ((tail_ + 1) & (N - 1)) == head_; }

public:
    bool push(const T& val) {
        std::size_t next = (tail_ + 1) & (N - 1);
        if (next == head_cached_) {
            head_cached_ = head_.load(std::memory_order_acquire);
            if (next == head_cached_)
                return false;
        }

        buf_[tail_] = val;
        tail_.store(next, std::memory_order_release);

        return true;
    }

    std::optional<T> pop() {
        if (head_ == tail_cached_) {
            tail_cached_ = tail_.load(std::memory_order_acquire);
            if (head_ == tail_cached_)
                return std::nullopt;
        }

        auto tmp = std::move(buf_[head_]);

        std::size_t next = (head_ + 1) & (N - 1);
        head_.store(next, std::memory_order_release);

        return tmp;
    }

private:
    std::unique_ptr<T[]> buf_;
    std::atomic<std::size_t> head_{0};
    std::atomic<std::size_t> tail_{0};
    std::size_t head_cached_{0};
    std::size_t tail_cached_{0};
};

} // namespace spscq::v04
