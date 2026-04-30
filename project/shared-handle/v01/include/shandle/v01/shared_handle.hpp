#pragma once

#include <atomic>
#include <utility>

#include <cassert>
#include <cstddef>

namespace shandle::v01 {

namespace detail {

template <typename T>
struct ControlBlock {
public:
    template <typename... Args>
    explicit ControlBlock(Args&&... args) {
        new (&obj_) T{std::forward<Args>(args)...};
    }

    ~ControlBlock() {}

public:
    T* get() noexcept { return reinterpret_cast<T*>(&obj_); }
    const T* get() const noexcept { return reinterpret_cast<const T*>(&obj_); }

    void destroy_object() noexcept { get()->~T(); }

public:
    std::atomic<int> refcount{1};

private:
    alignas(T) std::byte obj_[sizeof(T)];
};

} // namespace detail

template <typename T>
class SharedHandle {
    template <typename U, typename... Args>
    friend SharedHandle<U> make_handle(Args&&... args);

public:
    SharedHandle() noexcept : ctrl_{nullptr} {}

private:
    explicit SharedHandle(detail::ControlBlock<T>* ctrl) noexcept
        : ctrl_{ctrl} {}

public:
    ~SharedHandle() { release(); }

    SharedHandle(const SharedHandle& other) noexcept : ctrl_{other.ctrl_} {
        if (ctrl_) {
            ctrl_->refcount.fetch_add(1, std::memory_order_relaxed);
        }
    }
    SharedHandle& operator=(const SharedHandle& other) noexcept {
        if (this != &other) {
            release();

            ctrl_ = other.ctrl_;
            if (ctrl_) {
                ctrl_->refcount.fetch_add(1, std::memory_order_relaxed);
            }
        }
        return *this;
    }

    SharedHandle(SharedHandle&& other) noexcept : ctrl_{other.ctrl_} {
        other.ctrl_ = nullptr;
    }
    SharedHandle& operator=(SharedHandle&& other) noexcept {
        if (this != &other) {
            release();

            ctrl_ = other.ctrl_;
            other.ctrl_ = nullptr;
        }
        return *this;
    }

public:
    explicit operator bool() const noexcept { return ctrl_ != nullptr; }

public: // observer
    int use_count() const noexcept {
        if (!ctrl_) {
            return 0;
        }
        return ctrl_->refcount.load(std::memory_order_relaxed);
    }

public: // accessor
    T& operator*() const noexcept {
        assert(ctrl_ != nullptr);
        return *ctrl_->get();
    }
    T* operator->() const noexcept {
        assert(ctrl_ != nullptr);
        return ctrl_->get();
    }
    T* get() const noexcept {
        if (!ctrl_) {
            return nullptr;
        }
        return ctrl_->get();
    }

private:
    void release() noexcept {
        if (!ctrl_) {
            return;
        }

        if (ctrl_->refcount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            ctrl_->destroy_object();
            delete ctrl_;
        }
        ctrl_ = nullptr;
    }

private:
    detail::ControlBlock<T>* ctrl_;
};

template <typename T, typename... Args>
SharedHandle<T> make_handle(Args&&... args) {
    return SharedHandle<T>{
        new detail::ControlBlock<T>{std::forward<Args>(args)...}
    };
}

} // namespace shandle::v01
