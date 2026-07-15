#include "utils/timing_wheel.hpp"


adam::utils::TimingWheel::TimingWheel(uint64_t tick_ms, size_t slots, uint64_t now) noexcept
    : tick_ms_(tick_ms)
    , slots_(slots)
    , current_tick_(0)
    , last_now_(now)
    , buckets_(slots) {
    ASSERT(tick_ms_ > 0 && slots_ >= 2, "TimingWheel: tick_ms 必须>0, slots 必须>=2");
    for (auto& h : buckets_) {
        h.prev = h.next = &h;
    }
}


void
adam::utils::TimingWheel::advance(uint64_t now) noexcept {
    while (last_now_ + tick_ms_ <= now) {
        last_now_ += tick_ms_;
        ++current_tick_;

        Node* head = &buckets_[current_tick_ % slots_];
        for (Node* it = head->next; it != head; ) {
            Node* nx = it->next;
            if (it->rounds > 0) {
                --it->rounds;
            } else {
                unlink(it);
                Callback cb = std::move(it->cb);
                retire(it);
                cb();
            }
            it = nx;
        }
    }
}