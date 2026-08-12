#ifndef __ADAM_TCP_DIRECTORY_HPP__
#define __ADAM_TCP_DIRECTORY_HPP__


#include <cstdint>
#include <boost/unordered/concurrent_flat_map.hpp>


namespace adam::tcp {


/**
 * @brief 终端全局目录: uid -> 属主 reactor index
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


    Directory() noexcept = default;


    /**
     * @brief 登记/换属主: 返回旧属主 index, 之前不在线返回 NPOS
     */
    uint32_t
    exchange(uint32_t uid, uint32_t idx) noexcept {
        uint32_t prev = NPOS;

        // 已存在则进回调(拿旧值并覆盖), 不存在则插入 -- 两条路对单 key 都是原子的
        map_.insert_or_visit({uid, idx}, [&](auto& kv) {
            prev = kv.second;
            kv.second = idx;
        });

        return prev;
    }


    /**
     * @brief 仅当当前属主 == idx 时移除(防止旧属主的迟到清理误删新属主的登记)
     */
    bool
    erase_if(uint32_t uid, uint32_t idx) noexcept {
        return map_.erase_if(uid, [&](auto& kv) {
            return kv.second == idx;
        }) != 0;
    }


    /**
     * @brief 查属主 index, 不在线返回 NPOS
     */
    uint32_t
    get(uint32_t uid) noexcept {
        uint32_t idx = NPOS;

        map_.visit(uid, [&](const auto& kv) {
            idx = kv.second;
        });

        return idx;
    }


private:
    boost::concurrent_flat_map<uint32_t, uint32_t> map_;
}; // class Directory;


} // namespace adam::tcp


#endif // __ADAM_TCP_DIRECTORY_HPP__
