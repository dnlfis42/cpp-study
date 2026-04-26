#pragma once

#include <atomic>
#include <memory>
#include <optional>

#include <cstddef>
#include <utility>

namespace spscq::v02 {

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
    std::size_t size() const noexcept {
        auto h = head_.load(std::memory_order_relaxed);
        auto t = tail_.load(std::memory_order_relaxed);
        return (N + t - h) & (N - 1);
    }
    bool empty() const noexcept {
        return head_.load(std::memory_order_relaxed) ==
               tail_.load(std::memory_order_relaxed);
    }
    bool full() const noexcept {
        auto t = tail_.load(std::memory_order_relaxed);
        auto h = head_.load(std::memory_order_relaxed);
        return ((t + 1) & (N - 1)) == h;
    }

public:
    bool push(const T& val) {
        std::size_t head = head_.load(std::memory_order_acquire);
        if (((tail_ + 1) & (N - 1)) == head) {
            return false;
        }

        buf_[tail_] = val;

        std::size_t next = (tail_ + 1) & (N - 1);
        tail_.store(next, std::memory_order_release);

        return true;
    }
    std::optional<T> pop() {
        std::size_t tail = tail_.load(std::memory_order_acquire);
        if (tail == head_) {
            return std::nullopt;
        }

        auto tmp = std::move(buf_[head_]);

        std::size_t next = (head_ + 1) & (N - 1);
        head_.store(next, std::memory_order_release);

        return tmp;
    }

private:
    std::unique_ptr<T[]> buf_;
    std::atomic<std::size_t> head_;
    std::atomic<std::size_t> tail_;
};

} // namespace spscq::v02
