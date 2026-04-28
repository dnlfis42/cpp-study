#pragma once

#include "hazard_pointer.hpp"

#include <atomic>
#include <cstddef>
#include <optional>

namespace hp {

template <typename T, std::size_t MAX_THREADS = 64>
class LfStack {
private:
    struct Node {
        T val;
        Node* next;
        explicit Node(T v) : val(std::move(v)), next(nullptr) {}
    };

public:
    LfStack() = default;

    ~LfStack() {
        Node* curr = head_.load(std::memory_order_relaxed);
        while (curr) {
            Node* next = curr->next;
            delete curr;
            curr = next;
        }
        retire_list_.scan(hazard_table_);
    }

public:
    void push(T val) {
        Node* node = new Node(std::move(val));
        Node* old_head = head_.load(std::memory_order_relaxed);
        do {
            node->next = old_head;
        } while (!head_.compare_exchange_strong(
            old_head, node, std::memory_order_release, std::memory_order_relaxed
        ));
    }

    std::optional<T> pop() {
        std::size_t slot = hazard_table_.acquire_slot();
        Node* old_head;

        for (;;) {
            old_head = head_.load(std::memory_order_relaxed);
            if (old_head == nullptr) {
                hazard_table_.release_slot(slot);
                return std::nullopt;
            }

            hazard_table_.protect(slot, old_head);
            if (head_.load(std::memory_order_acquire) != old_head) {
                continue;
            }

            Node* next = old_head->next;
            if (head_.compare_exchange_strong(
                    old_head, next, std::memory_order_acquire,
                    std::memory_order_relaxed
                )) {
                break;
            }
        }

        T val = std::move(old_head->val);
        hazard_table_.clear(slot);
        retire_list_.retire(old_head, delete_node, hazard_table_);
        hazard_table_.release_slot(slot);
        return val;
    }

private:
    static void delete_node(void* ptr) { delete static_cast<Node*>(ptr); }

private:
    std::atomic<Node*> head_{nullptr};
    HazardTable<MAX_THREADS> hazard_table_;

    static thread_local RetireList<MAX_THREADS> retire_list_;
};

template <typename T, std::size_t MAX_THREADS>
thread_local RetireList<MAX_THREADS> LfStack<T, MAX_THREADS>::retire_list_;

} // namespace hp
