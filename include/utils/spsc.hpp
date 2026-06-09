#ifndef __TYPHON_UTILS_SPSC_HPP__
#define __TYPHON_UTILS_SPSC_HPP__


#include <atomic>
#include <cstddef>
#include <cassert>
#include <utility>
#include <algorithm>


namespace typhon::utils {


/**
 * @brief 单生产者单消费者无锁环形队列 (bounded SPSC ring buffer)。
 *        生产者和消费者必须各自固定在一个线程里调用，**不支持多生产者 / 多消费者**。
 *
 * @tparam T 元素类型，需可移动 / 可拷贝赋值 (典型用裸指针，trivial 拷贝)
 * @tparam N 队列容量，必须是 2 的幂
 */
template<typename T, size_t N = 4096>
class SPSC {
    static_assert(N > 0 && (N & (N - 1)) == 0, "N 必须是 2 的幂次方");

    static constexpr size_t MASK       = N - 1;
    static constexpr size_t CACHE_LINE = 64;

public:
    SPSC() noexcept {
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
    }


    ~SPSC() noexcept = default;


    SPSC(const SPSC&) = delete;
    SPSC& operator=(const SPSC&) = delete;
    SPSC(SPSC&&) = delete;
    SPSC& operator=(SPSC&&) = delete;


    /**
     * @brief 入队，仅生产者线程调用。左值拷贝 / 右值移动两个重载。
     * @return 成功 true；队列满 false
     */
    bool
    enqueue(const T& val) noexcept {
        return push(val);
    }


    bool
    enqueue(T&& val) noexcept {
        return push(std::move(val));
    }


    /**
     * @brief 出队，仅消费者线程调用。
     * @return 成功 true；队列空 false
     */
    bool
    dequeue(T& val) noexcept {
        const size_t tail = tail_.load(std::memory_order_relaxed);

        if (tail == cached_head_) {
            cached_head_ = head_.load(std::memory_order_acquire);
            if (tail == cached_head_) {
                return false;
            }
        }

        val = std::move(slots_[tail & MASK]);
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }


    /**
     * @brief 批量出队，仅消费者线程调用。一次最多取 max 个，连续顺序拷出。
     * @return 实际取出的个数(0 表示空)
     */
    size_t
    try_dequeue_bulk(T* buf, size_t max) noexcept {
        const size_t tail = tail_.load(std::memory_order_relaxed);

        // 先用 cached_head_，耗尽了才跨核 load head_(和 dequeue 一致，省一次 atomic load)
        if (tail == cached_head_) {
            cached_head_ = head_.load(std::memory_order_acquire);
            if (tail == cached_head_) {
                return 0;
            }
        }

        const size_t count = std::min(cached_head_ - tail, max);
        for (size_t i = 0; i < count; ++i) {
            buf[i] = std::move(slots_[(tail + i) & MASK]);
        }

        tail_.store(tail + count, std::memory_order_release);
        return count;
    }


    /**
     * @brief 把队列里剩余元素逐个交给 handle 处理后清空。
     *        **仅消费者线程 / 停机路径调用**(它内部走 dequeue 语义)。
     * @param handle 可调用对象 (T&) -> void；模板传入，无 std::function 类型擦除开销
     */
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


    bool
    empty() const noexcept {
        return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
    }


    /**
     * @brief 近似元素个数 —— 并发下是瞬时快照，仅供监控 / 调试，不可用于逻辑判断。
     */
    size_t
    size() const noexcept {
        const size_t head = head_.load(std::memory_order_acquire);
        const size_t tail = tail_.load(std::memory_order_acquire);
        return head - tail;
    }


    static constexpr size_t
    capacity() noexcept {
        return N;
    }


private:
    /**
     * @brief enqueue 的公共实现，完美转发左 / 右值，避免两个重载重复满判断逻辑。
     */
    template<typename U>
    bool
    push(U&& val) noexcept {
        const size_t head = head_.load(std::memory_order_relaxed);

        if (head - cached_tail_ >= N) {
            cached_tail_ = tail_.load(std::memory_order_acquire);
            if (head - cached_tail_ >= N) {
                return false;
            }
        }

        slots_[head & MASK] = std::forward<U>(val);
        head_.store(head + 1, std::memory_order_release);
        return true;
    }


    alignas(CACHE_LINE) T slots_[N];
    alignas(CACHE_LINE) std::atomic<size_t> head_;
    size_t cached_tail_ { 0 };
    [[maybe_unused]] char pad_producer_[CACHE_LINE - sizeof(std::atomic<size_t>) - sizeof(size_t)];
    alignas(CACHE_LINE) std::atomic<size_t> tail_;
    size_t cached_head_ { 0 };
    [[maybe_unused]] char pad_consumer_[CACHE_LINE - sizeof(std::atomic<size_t>) - sizeof(size_t)];
}; // class SPSC;


} // namespace typhon::utils


#endif // __TYPHON_UTILS_SPSC_HPP__
