#pragma once

#include <memory>
#include <mutex>
#include <optional>

#include <cassert>
#include <cstddef>
#include <utility>

namespace spscq::v01 {

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

public: // 상태
    std::size_t capacity() const noexcept { return N; }
    std::size_t size() const noexcept { return (N + tail_ - head_) & (N - 1); }
    bool empty() const noexcept { return head_ == tail_; }
    bool full() const noexcept { return ((tail_ + 1) & (N - 1)) == head_; }

public:
    bool push(const T& val) {
        std::lock_guard lock{mutex_};
        if (full()) {
            return false;
        }

        buf_[tail_] = val;
        tail_ = (tail_ + 1) & (N - 1);

        return true;
    }

    std::optional<T> pop() {
        std::lock_guard lock{mutex_};
        if (empty()) {
            return std::nullopt;
        }

        auto tmp = std::move(buf_[head_]);
        head_ = (head_ + 1) & (N - 1);

        return tmp;
    }

private:
    std::unique_ptr<T[]> buf_;
    std::size_t head_;
    std::size_t tail_;
    mutable std::mutex mutex_;
};

} // namespace spscq::v01
