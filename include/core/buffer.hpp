#ifndef __TYPHON_CORE_BUFFER_HPP__
#define __TYPHON_CORE_BUFFER_HPP__


#include <deque>
#include "core/typhon.in.hpp"
#include "core/package.hpp"
#include "utils/cryptor.hpp"


namespace typhon::core {


/** 
 * @brief UDP 写缓冲区
 * 
 * 用于 kcp 中 output 中发送消息
 */
struct SndBuf {
    typedef std::deque<SndBuf*> Que; ///< SndBuf 队列


    ::sockaddr_storage addr;         ///< 对端地址
    ::socklen_t        addrlen;      ///< 地址长度
    uint32_t           len;          ///< wire 数据报长度(含 8B SipHash 信封, 由 ikcp_output 加好)
    uint64_t           time;         ///< 缓冲区创建的时间
    uint8_t            buf[UDP_MTU]; ///< 直接存 ikcp 产出的完整 wire 数据报 [8B MAC][KCP datagram]


    explicit
    SndBuf(const void* addr, ::socklen_t addrlen, const char* b, uint32_t l, uint64_t time) noexcept
        : addrlen(addrlen)
        , len(l)
        , time(time) {
        ::memcpy(&this->addr, addr, addrlen);
        ASSERT(l <= UDP_MTU, "len = {}, max = {}", l, UDP_MTU);
        ::memcpy(this->buf, b, l);
    }


    SndBuf(const SndBuf&) = delete;
    SndBuf& operator=(const SndBuf&) = delete;
    SndBuf(SndBuf&&) = delete;
    SndBuf& operator=(SndBuf&&) = delete;
}; // struct SndBuf;


constexpr uint32_t RCVBUF_INIT = 4 * 1024;
constexpr uint32_t RCVBUF_MAX  = PKG_MAX_LEN + 8 * 1024;


/**
 * @brief 接收缓冲, 用于 TCP 读缓冲区(动态扩容 linear buffer)
 */
struct Buffer {
    uint32_t rpos { 0 };       ///< 读游标
    uint32_t wpos { 0 };       ///< 写游标
    uint32_t cap  { 0 };       ///< 当前已分配容量(0 = 未分配)
    uint8_t* buf  { nullptr }; ///< 缓冲区


    explicit
    Buffer() noexcept
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
     * @return 成功返回 xOK. 内存分配失败则 abort
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


    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    Buffer(Buffer&&) = delete;
    Buffer& operator=(Buffer&&) = delete;
}; // struct RcvBuf;


} // namespace typhon::core;


#endif // __TYPHON_CORE_BUFFER_HPP__