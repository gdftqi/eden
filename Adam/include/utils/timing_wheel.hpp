#ifndef __ADAM_UTILS_TIMING_WHEEL_HPP__
#define __ADAM_UTILS_TIMING_WHEEL_HPP__


#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <vector>

#include "utils/log.hpp"


namespace adam::utils {


class TimingWheel {
    TimingWheel(const TimingWheel&)            = delete;
    TimingWheel& operator=(const TimingWheel&) = delete;
    TimingWheel(TimingWheel&&)                 = delete;
    TimingWheel& operator=(TimingWheel&&)      = delete;


    struct Node {
        Node*    prev = nullptr;   ///< 双向循环链表前驱
        Node*    next = nullptr;   ///< 双向循环链表后继
        uint64_t rounds = 0;       ///< 还需转几圈
        uint64_t gen    = 0;       ///< generation: 每次回收 +1, 句柄据此判失效
        bool     active = false;   ///< 是否挂在轮上(触发/取消后 false)
        std::function<void()> cb;  ///< 到期回调
    };


public:
    typedef std::function<void()> Callback;


    struct Handle {
        Node*    node = nullptr;
        uint64_t gen  = 0;
    };


    explicit
    TimingWheel(uint64_t tick_ms, size_t slots, uint64_t now) noexcept;


    Handle
    add(uint64_t delay_ms, Callback cb) noexcept {
        uint64_t ticks = (delay_ms + tick_ms_ - 1) / tick_ms_;
        if (ticks == 0) {
            ticks = 1;
        }

        size_t   slot   = (current_tick_ + ticks) % slots_;
        uint64_t rounds = (ticks - 1) / slots_;

        Node* n  = alloc_node();
        n->rounds = rounds;
        n->active = true;
        n->cb     = std::move(cb);
        link(&buckets_[slot], n);

        return Handle{ n, n->gen };
    }


    void
    cancel(const Handle& h) noexcept {
        Node* n = h.node;
        if (n == nullptr || n->gen != h.gen || !n->active) {
            return;
        }

        unlink(n);
        retire(n);
    }


    void
    advance(uint64_t now) noexcept;


    size_t
    pool_capacity() const noexcept {
        return storage_.size();
    }


private:
    static void
    link(Node* head, Node* n) noexcept {
        n->prev = head->prev;
        n->next = head;
        head->prev->next = n;
        head->prev = n;
    }


    static void
    unlink(Node* n) noexcept {
        n->prev->next = n->next;
        n->next->prev = n->prev;
        n->prev = n->next = nullptr;
    }


    Node*
    alloc_node() noexcept {
        if (free_ != nullptr) {
            Node* n = free_;
            free_   = n->next;
            return n;
        }

        storage_.emplace_back();
        return &storage_.back();
    }


    void
    retire(Node* n) noexcept {
        n->cb     = nullptr;
        n->active = false;
        ++n->gen;
        n->prev   = nullptr;
        n->next   = free_;
        free_     = n;
    }


    uint64_t          tick_ms_;
    size_t            slots_;
    uint64_t          current_tick_;             ///< 累计 tick; 当前格 = % slots_
    uint64_t          last_now_;
    std::vector<Node> buckets_;                  ///< 每格一个哨兵(固定大小, 地址稳定)
    std::deque<Node>  storage_;                  ///< 节点存储(地址稳定)
    Node*             free_ = nullptr;           ///< freelist 头(用 next 串)
}; // class TimingWheel;


} // namespace adam::utils


#endif // __ADAM_UTILS_TIMING_WHEEL_HPP__
