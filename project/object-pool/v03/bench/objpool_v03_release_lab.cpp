#include <benchmark/benchmark.h>

#include <memory>
#include <vector>

#include <cassert>
#include <cstddef>

// NaivePool: reinterpret_cast<Node*>(obj)로 T*->Node* 역산.
// standard-layout T에서만 pointer-interconvertible 보장.
// sizeof(Deleter) = 8, sizeof(Handle) = 16.
namespace naive {

template <typename T>
class ObjectPool {
    static constexpr std::size_t SENTINEL = std::size_t(-1);

public:
    class Deleter {
    public:
        Deleter() noexcept : pool_{nullptr} {}
        explicit Deleter(ObjectPool* pool) noexcept : pool_{pool} {}

    public:
        void operator()(T* obj) const noexcept {
            if (pool_ != nullptr && obj != nullptr) {
                pool_->release(obj);
            }
        }

    private:
        ObjectPool* pool_;
    };

    using Handle = std::unique_ptr<T, Deleter>;

private:
    struct Node {
        T data;
        std::size_t next;
    };

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

    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;
    ObjectPool(ObjectPool&&) = delete;
    ObjectPool& operator=(ObjectPool&&) = delete;

public:
    [[nodiscard]]
    Handle acquire() noexcept {
        if (head_free_ == SENTINEL) {
            return Handle{};
        }

        const std::size_t idx = head_free_;
        head_free_ = storage_[idx].next;
        --available_;

        return Handle{&storage_[idx].data, Deleter{this}};
    }

private:
    void release(T* obj) noexcept {
        Node* node = reinterpret_cast<Node*>(obj);
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

} // namespace naive

// FixedPool: Deleter가 Node*를 직접 보유: reinterpret_cast 없음.
// sizeof(Deleter) = 16, sizeof(Handle) = 24.
namespace fixed {

template <typename T>
class ObjectPool {
    static constexpr std::size_t SENTINEL = std::size_t(-1);

private:
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
        void operator()(T* /*obj*/) const noexcept {
            if (pool_ != nullptr) {
                pool_->release(node_);
            }
        }

    private:
        ObjectPool* pool_;
        Node* node_;
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

    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;
    ObjectPool(ObjectPool&&) = delete;
    ObjectPool& operator=(ObjectPool&&) = delete;

public:
    [[nodiscard]]
    Handle acquire() noexcept {
        if (head_free_ == SENTINEL) {
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

} // namespace fixed

namespace {

struct Item {
    char buf[64];
};

} // namespace

static void BM_Naive_Handle(benchmark::State& state) {
    naive::ObjectPool<Item> pool{64};

    for (auto _ : state) {
        auto h = pool.acquire();
        benchmark::DoNotOptimize(h);
    }
}
BENCHMARK(BM_Naive_Handle);

static void BM_Fixed_Handle(benchmark::State& state) {
    fixed::ObjectPool<Item> pool{64};

    for (auto _ : state) {
        auto h = pool.acquire();
        benchmark::DoNotOptimize(h);
    }
}
BENCHMARK(BM_Fixed_Handle);

BENCHMARK_MAIN();
