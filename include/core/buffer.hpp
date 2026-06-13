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
    uint32_t           len;          ///< 消息长度
    uint64_t           time;         ///< 缓冲区创建的时间
    uint64_t*          siphash;      ///< buf[0:8) 的数据, siphash 签名段
    uint8_t            buf[UDP_MTU]; ///< 缓冲区大小


    explicit
    SndBuf(const void* addr, ::socklen_t addrlen, const char* b, uint32_t l, uint64_t time) noexcept
        : addrlen(addrlen)
        , len(l + ENVELOPE_MAC_LEN)
        , time(time) {
        siphash = (uint64_t*)this->buf;
        ::memcpy(&this->addr, addr, addrlen);
        ASSERT(l <= KCP_MTU, "len = {}, max = {}", l, KCP_MTU);
        ::memcpy(this->buf + ENVELOPE_MAC_LEN, b, l);
    }


    SndBuf(const SndBuf&) = delete;
    SndBuf& operator=(const SndBuf&) = delete;
    SndBuf(SndBuf&&) = delete;
    SndBuf& operator=(SndBuf&&) = delete;
}; // struct SndBuf;


// RcvBuf 动态扩容参数
constexpr uint32_t RCVBUF_INIT = 4 * 1024;                ///< 初始容量(首次 append 时 lazy 分配)
constexpr uint32_t RCVBUF_MAX  = PKG_MAX_LEN + 8 * 1024;  ///< 容量封顶 = 最大单包 + 一次 read 余量(防 OOM DoS)


/**
 * @brief 接收缓冲, 用于 TCP 读缓冲区(动态扩容 linear buffer)
 */
struct RcvBuf {
    uint32_t rpos { 0 };       ///< 读游标
    uint32_t wpos { 0 };       ///< 写游标
    uint32_t cap  { 0 };       ///< 当前已分配容量(0 = 未分配)
    uint8_t* buf  { nullptr };


    explicit
    RcvBuf() noexcept
    {}


    ~RcvBuf() noexcept {
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
     * @brief 追加数据
     *
     * @return 成功返回 xOK. 内存分配失败则 abort
     */
    int
    append(const uint8_t* data, uint32_t len) noexcept;


    /**
     * @brief 解码
     *
     * @return xOK 取到一个完整包 / xAGAIN 半包
     */
    int
    decode(core::PackageEx** pkx) noexcept;


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


    RcvBuf(const RcvBuf&) = delete;
    RcvBuf& operator=(const RcvBuf&) = delete;
    RcvBuf(RcvBuf&&) = delete;
    RcvBuf& operator=(RcvBuf&&) = delete;
}; // struct RcvBuf;


} // namespace typhon::core;


#endif // __TYPHON_CORE_BUFFER_HPP__