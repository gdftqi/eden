#ifndef __ADAM_TCP_DIRECTORY_HPP__
#define __ADAM_TCP_DIRECTORY_HPP__


#include <mutex>
#include <cstdint>
#include <absl/container/flat_hash_map.h>


namespace adam::tcp {


/**
 * @brief 终端全局目录: uid → 属主 reactor index
 *
 * 只在终端进入/顶号/下线时读写(控制面低频), 分片锁足够;
 * exchange 把"查旧属主+登记新属主"并成一步原子操作,
 * 两个 reactor 同时处理同一 uid 的 ENT 由此串行化。
 * 热路径(每消息)只走各 reactor 私有的 terminal_router_, 不碰这里。
 */
class Directory {
    Directory(const Directory&) = delete;
    Directory& operator=(const Directory&) = delete;
    Directory(Directory&&) = delete;
    Directory& operator=(Directory&&) = delete;


public:
    /**
     * @brief "无属主"哨兵(reactor 数远小于 uint32 上限, 不会撞)
     */
    static constexpr uint32_t NPOS = (uint32_t)-1;


    /**
     * @brief 分片数, 2 的幂(按 uid 低位取分片)
     */
    static constexpr uint32_t SHARDS = 16;


    Directory() noexcept = default;


    /**
     * @brief 登记/换属主: 返回旧属主 index, 之前不在线返回 NPOS
     */
    uint32_t
    exchange(uint32_t uid, uint32_t idx) noexcept {
        auto& sd = shard(uid);
        std::lock_guard<std::mutex> lk(sd.mtx);

        auto itr = sd.map.find(uid);
        if (itr == sd.map.end()) {
            sd.map.emplace(uid, idx);
            return NPOS;
        }

        uint32_t prev = itr->second;
        itr->second = idx;
        return prev;
    }


    /**
     * @brief 仅当当前属主 == idx 时移除(防止旧属主的迟到清理误删新属主的登记)
     */
    bool
    erase_if(uint32_t uid, uint32_t idx) noexcept {
        auto& sd = shard(uid);
        std::lock_guard<std::mutex> lk(sd.mtx);

        auto itr = sd.map.find(uid);
        if (itr == sd.map.end() || itr->second != idx) {
            return false;
        }

        sd.map.erase(itr);
        return true;
    }


    /**
     * @brief 查属主 index, 不在线返回 NPOS
     */
    uint32_t
    get(uint32_t uid) noexcept {
        auto& sd = shard(uid);
        std::lock_guard<std::mutex> lk(sd.mtx);

        auto itr = sd.map.find(uid);
        return itr == sd.map.end() ? NPOS : itr->second;
    }


private:
    struct Shard {
        std::mutex                              mtx;
        absl::flat_hash_map<uint32_t, uint32_t> map;
    };


    Shard&
    shard(uint32_t uid) noexcept {
        return shards_[uid & (SHARDS - 1)];
    }


    Shard shards_[SHARDS];
}; // class Directory;


} // namespace adam::tcp


#endif // __ADAM_TCP_DIRECTORY_HPP__
