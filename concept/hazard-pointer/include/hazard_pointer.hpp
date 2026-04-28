#pragma once

#include <atomic>
#include <functional>
#include <vector>

#include <cstddef>

namespace hp {

template <std::size_t MAX_THREADS = 64>
class HazardTable {
public:
    HazardTable() {
        for (auto& slot : hazard_table_)
            slot.store(nullptr);
        for (auto& occ : occupied_)
            occ.store(false);
    }

public:
    std::vector<void*> collect() const {
        std::vector<void*> result;
        for (std::size_t i = 0; i < MAX_THREADS; ++i) {
            void* p = hazard_table_[i].load(std::memory_order_seq_cst);
            if (p != nullptr)
                result.push_back(p);
        }
        return result;
    }

public:
    std::size_t acquire_slot() {
        std::size_t idx = 0;
        for (;;) {
            bool expected = false;
            if (occupied_[idx].compare_exchange_strong(expected, true))
                return idx;
            if (++idx == MAX_THREADS)
                idx = 0;
        }
    }

    void release_slot(std::size_t slot) {
        occupied_[slot].store(false, std::memory_order_relaxed);
    }

    void protect(std::size_t slot, void* ptr) {
        hazard_table_[slot].store(ptr, std::memory_order_seq_cst);
    }

    void clear(std::size_t slot) {
        hazard_table_[slot].store(nullptr, std::memory_order_relaxed);
    }

private:
    std::atomic<void*> hazard_table_[MAX_THREADS];
    std::atomic<bool> occupied_[MAX_THREADS];
};

template <std::size_t MAX_THREADS = 64>
class RetireList {
private:
    struct RetiredNode {
        void* ptr;
        std::function<void(void*)> deleter;
    };

public:
    ~RetireList() {
        for (auto& node : list_)
            node.deleter(node.ptr);
    }

public:
    void retire(
        void* ptr, std::function<void(void*)> deleter,
        HazardTable<MAX_THREADS>& ht
    ) {
        list_.emplace_back(RetiredNode{ptr, std::move(deleter)});
        if (list_.size() > MAX_THREADS * 2)
            scan(ht);
    }

    void scan(HazardTable<MAX_THREADS>& ht) {
        auto hazard = ht.collect();
        std::vector<RetiredNode> survivors;

        for (auto& node : list_) {
            bool in_hazard = false;
            for (auto ptr : hazard) {
                if (ptr == node.ptr) {
                    in_hazard = true;
                    break;
                }
            }
            if (in_hazard)
                survivors.push_back(std::move(node));
            else
                node.deleter(node.ptr);
        }

        list_ = std::move(survivors);
    }

private:
    std::vector<RetiredNode> list_;
};

} // namespace hp
