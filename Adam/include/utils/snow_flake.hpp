#ifndef __ADAM_UTILS_SNOW_FLAKE_HPP__
#define __ADAM_UTILS_SNOW_FLAKE_HPP__

#include <atomic>
#include <chrono>
#include <cstdint>

#include "utils/log.hpp"


namespace adam::utils {


/**
 * 雪花发号器: 64 位全局唯一且趋势递增的 ID.
 *
 *   [ 0 | 41 位毫秒时间戳 | 10 位节点号 | 12 位毫秒内序号 ]
 *
 * - 时间戳基于自定义纪元(2026-01-01 UTC), 41 位可用约 69 年
 * - 节点号区分进程(0~1023), 同一节点内靠 CAS 保证唯一, 跨节点靠它隔离
 * - 单毫秒内最多 4096 个, 用尽则自旋等下一毫秒
 *
 * 线程安全: 无锁, 整个 (时间戳, 序号) 打包进一个 atomic 用 CAS 推进,
 * 线程安全, 允许多线程共用同一个实例.
 */
class Snowflake {
    Snowflake(const Snowflake&)            = delete;
    Snowflake& operator=(const Snowflake&) = delete;
    Snowflake(Snowflake&&)                 = delete;
    Snowflake& operator=(Snowflake&&)      = delete;


    static constexpr uint64_t EPOCH_MS  = 1767225600000ULL;  ///< 2026-01-01 00:00:00 UTC
    static constexpr uint64_t NODE_BITS = 10;
    static constexpr uint64_t SEQ_BITS  = 12;
    static constexpr uint64_t NODE_MAX  = (1ULL << NODE_BITS) - 1;
    static constexpr uint64_t SEQ_MASK  = (1ULL << SEQ_BITS) - 1;


public:
    explicit
    Snowflake(uint32_t node_id) noexcept
        : node_(uint64_t(node_id) << SEQ_BITS) {
        ASSERT(node_id <= NODE_MAX, "node_id 超界: {} > {}", node_id, NODE_MAX);
    }


    /**
     * @brief 取下一个 ID
     */
    int64_t
    next() noexcept {
        for (;;) {
            uint64_t cur     = state_.load(std::memory_order_relaxed);
            uint64_t last_ts = cur >> SEQ_BITS;
            uint64_t seq     = cur & SEQ_MASK;
            uint64_t now     = now_ms();

            uint64_t nts, nseq;
            if (now > last_ts) {
                nts  = now;
                nseq = 0;
            } else if (seq < SEQ_MASK) {
                nts  = last_ts;
                nseq = seq + 1;
            } else {
                continue;
            }

            // CAS 失败说明别的线程抢先发走了一个号, 重来即可
            uint64_t next_state = (nts << SEQ_BITS) | nseq;
            if (state_.compare_exchange_weak(cur, next_state,
                                             std::memory_order_relaxed)) {
                return int64_t((nts << (NODE_BITS + SEQ_BITS)) | node_ | nseq);
            }
        }
    }


private:
    static uint64_t
    now_ms() noexcept {
        using namespace std::chrono;
        uint64_t ms = uint64_t(duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
        return ms - EPOCH_MS;
    }


    const uint64_t        node_;        ///< 预先移好位的节点号
    std::atomic<uint64_t> state_ = 0;   ///< [ 41 位时间戳 | 12 位序号 ] 打包
}; // class Snowflake;


} // namespace adam::utils


#endif // __ADAM_UTILS_SNOW_FLAKE_HPP__
