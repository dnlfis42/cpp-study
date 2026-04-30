#pragma once

#include <atomic>
#include <type_traits>
#include <utility>

#include <cassert>

namespace shandle::v02 {

class IntrusiveBase;

template <typename T>
concept Intrusive = std::is_base_of_v<IntrusiveBase, T>;

class IntrusiveBase {
    template <Intrusive T>
    friend class SharedHandle;

private:
    std::atomic<int> refcount{1};
};

template <Intrusive T>
class SharedHandle {
    template <Intrusive U, typename... Args>
    friend SharedHandle<U> make_handle(Args&&... args);

public:
    SharedHandle() noexcept : ptr_{nullptr} {}

private:
    explicit SharedHandle(T* ptr) noexcept : ptr_{ptr} {}

public:
    ~SharedHandle() { release(); }

    SharedHandle(const SharedHandle& other) noexcept : ptr_{other.ptr_} {
        if (ptr_) {
            ptr_->refcount.fetch_add(1, std::memory_order_relaxed);
        }
    }

    SharedHandle& operator=(const SharedHandle& other) noexcept {
        if (this != &other) {
            release();

            ptr_ = other.ptr_;
            if (ptr_) {
                ptr_->refcount.fetch_add(1, std::memory_order_relaxed);
            }
        }
        return *this;
    }

    SharedHandle(SharedHandle&& other) noexcept : ptr_{other.ptr_} {
        other.ptr_ = nullptr;
    }

    SharedHandle& operator=(SharedHandle&& other) noexcept {
        if (this != &other) {
            release();

            ptr_ = other.ptr_;
            other.ptr_ = nullptr;
        }
        return *this;
    }

public:
    explicit operator bool() const noexcept { return ptr_ != nullptr; }

public: // observer
    int use_count() const noexcept {
        if (!ptr_) {
            return 0;
        }
        return ptr_->refcount.load(std::memory_order_relaxed);
    }

public: // accessor
    T& operator*() const noexcept {
        assert(ptr_ != nullptr);
        return *ptr_;
    }
    T* operator->() const noexcept {
        assert(ptr_ != nullptr);
        return ptr_;
    }
    T* get() const noexcept { return ptr_; }

private:
    void release() noexcept {
        if (!ptr_) {
            return;
        }

        if (ptr_->refcount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            delete ptr_;
        }
        ptr_ = nullptr;
    }

private:
    T* ptr_;
};

template <Intrusive T, typename... Args>
SharedHandle<T> make_handle(Args&&... args) {
    return SharedHandle<T>{new T{std::forward<Args>(args)...}};
}

} // namespace shandle::v02
