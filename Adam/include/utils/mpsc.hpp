#ifndef __ADAM_UTILS_MPSC_HPP__
#define __ADAM_UTILS_MPSC_HPP__


#include <atomic>
#include <cstddef>
#include <utility>


namespace adam::utils {


/**
 * @brief MPSC lockfree performance
 */
template<typename T, size_t N = 4096>
class MPSC {
    static_assert(N > 0 && (N & (N - 1)) == 0, "N 必须是 2 的幂次方");

    static constexpr size_t MASK       = N - 1;
    static constexpr size_t CACHE_LINE = 64;


public:
    explicit
    MPSC() noexcept {
        for (size_t i = 0; i < N; ++i) {
            slots_[i].seq.store(i, std::memory_order_relaxed);
        }
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
    }


    ~MPSC() noexcept = default;


    MPSC(const MPSC&) = delete;
    MPSC& operator=(const MPSC&) = delete;
    MPSC(MPSC&&) = delete;
    MPSC& operator=(MPSC&&) = delete;


    bool
    enqueue(const T& val) noexcept {
        return push(val);
    }


    bool
    enqueue(T&& val) noexcept {
        return push(std::move(val));
    }


    bool
    dequeue(T& val) noexcept {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        Slot& s = slots_[tail & MASK];

        if (s.seq.load(std::memory_order_acquire) != tail + 1) {
            return false;
        }

        val = std::move(s.val);
        s.seq.store(tail + N, std::memory_order_release);
        tail_.store(tail + 1, std::memory_order_relaxed);
        return true;
    }


    size_t
    try_dequeue_bulk(T* buf, size_t max) noexcept {
        const size_t tail = tail_.load(std::memory_order_relaxed);

        size_t n = 0;
        for (; n < max; ++n) {
            const size_t pos = tail + n;
            Slot& s = slots_[pos & MASK];

            if (s.seq.load(std::memory_order_acquire) != pos + 1) {
                break;
            }

            buf[n] = std::move(s.val);
            s.seq.store(pos + N, std::memory_order_release);
        }

        if (n > 0) {
            tail_.store(tail + n, std::memory_order_relaxed);
        }

        return n;
    }


    template<typename Fn>
    void
    clear(Fn&& handle) noexcept {
        constexpr int BATCH = 16;
        T es[BATCH];
        size_t n;
        while ((n = try_dequeue_bulk(es, BATCH)) > 0) {
            for (size_t i = 0; i < n; ++i) {
                handle(std::move(es[i]));
            }
        }
    }


    static constexpr size_t
    capacity() noexcept {
        return N;
    }


private:
    struct Slot {
        std::atomic<size_t> seq;
        T                   val;
    };


    template<typename U>
    bool
    push(U&& val) noexcept {
        size_t pos = head_.load(std::memory_order_relaxed);

        for (;;) {
            Slot& s = slots_[pos & MASK];
            const size_t   seq = s.seq.load(std::memory_order_acquire);
            const intptr_t dif = static_cast<intptr_t>(seq - pos);

            if (dif == 0) {
                if (head_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    s.val = std::forward<U>(val);
                    s.seq.store(pos + 1, std::memory_order_release);
                    return true;
                }
            } else if (dif < 0) {
                return false;
            } else {
                pos = head_.load(std::memory_order_relaxed);
            }
        }
    }


    alignas(CACHE_LINE) Slot slots_[N];
    alignas(CACHE_LINE) std::atomic<size_t> head_;
    alignas(CACHE_LINE) std::atomic<size_t> tail_;
}; // class MPSC;


} // namespace adam::utils


#endif // __ADAM_UTILS_MPSC_HPP__
