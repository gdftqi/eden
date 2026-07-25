#ifndef __ADAM_TCP_BUFFER_HPP__
#define __ADAM_TCP_BUFFER_HPP__


#include "core/adam.in.hpp"
#include "core/package.hpp"


namespace adam::tcp {


/**
 * @brief TCP缓冲区
 */
struct Buffer {
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    Buffer(Buffer&&) = delete;
    Buffer& operator=(Buffer&&) = delete;


    /**
     * @brief 缓冲区初始化值
     */
    static constexpr uint32_t INIT_SIZE = 4 * 1024;


    /**
     * @brief 缓冲区最大值
     */
    static constexpr uint32_t MAX_SIZE = core::PKG_MAX_LEN + 1024 * 1024;


    /**
     * @brief limit 传此值表示不设上限(仅受可用内存与 uint32 容量约束)
     */
    static constexpr uint32_t UNLIMITED = 0;


    uint32_t rpos  { 0 };        // 读游标
    uint32_t wpos  { 0 };        // 写游标
    uint32_t cap   { 0 };        // 当前已分配容量(0 = 未分配)
    uint32_t limit { MAX_SIZE }; // 本实例扩容上限; UNLIMITED(0) = 无上限
    uint8_t* buf   { nullptr };  // 缓冲区


    explicit
    Buffer(uint32_t max_size = MAX_SIZE) noexcept
        : limit(max_size)
    {}


    ~Buffer() noexcept {
        if (buf) {
            ::mi_free(buf);
        }
    }


    /**
     * @brief 可读的数据长度
     */
    size_t
    readable() const noexcept {
        return wpos - rpos;
    }


    /**
     * @brief 可写的数据长度
     */
    size_t
    writable() const noexcept {
        return cap - wpos;
    }


    /**
     * @brief 可读区起始指针(配合 readable() 长度使用); 无可读数据时返回 nullptr
     */
    const uint8_t*
    peek() const noexcept {
        return readable() > 0 ? buf + rpos : nullptr;
    }


    /**
     * @brief 消费掉已读的 n 字节(推进读游标); 全部读完后游标归零, 回收前缀死区
     */
    void
    consume(uint32_t n) noexcept {
        rpos += n;
        if (rpos >= wpos) {
            rpos = wpos = 0;
        }
    }


    /**
     * @brief 追加数据
     *
     * @return 成功 xOK; 超过上限(limit)返回 xERR; 分配失败 abort
     */
    int
    append(const uint8_t* data, uint32_t len) noexcept;


    void
    compact() noexcept {
        if (rpos == 0) {
            return;
        }

        size_t remaining = readable();
        if (remaining > 0) {
            ::memmove(buf, buf + rpos, remaining);
        }

        rpos = 0;
        wpos = remaining;
    }
}; // struct Buffer;


} // namespace adam::tcp


#endif // __ADAM_TCP_BUFFER_HPP__
