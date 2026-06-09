#ifndef __TYPHON_CORE_BUFFER_HPP__
#define __TYPHON_CORE_BUFFER_HPP__


#include <deque>
#include "core/typhon.in.hpp"
#include "core/package.hpp"
#include "utils/cryptor.hpp"


namespace typhon::core {


/** 
 * @brief 发送缓冲区
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
}; // class SndBuf;


/**
 * @brief 接收缓冲
 * 
 * buf 要么是 nullptr(从未收到数据), 要么是 PKG_MAX_LEN(65535) 大小的堆区
 */
struct RcvBuf {
    uint32_t rpos { 0 };
    uint32_t wpos { 0 };
    uint8_t* buf  { nullptr };


    explicit
    RcvBuf() noexcept
    {}


    ~RcvBuf() noexcept {
        if (buf) {
            ::mi_free(buf);
        }
    }


    RcvBuf(const RcvBuf&) = delete;
    RcvBuf& operator=(const RcvBuf&) = delete;
    RcvBuf(RcvBuf&&) = delete;
    RcvBuf& operator=(RcvBuf&&) = delete;


    /**
     * @brief 返回缓冲区中可读的数据长度
     */
    size_t
    readable() const noexcept {
        return wpos - rpos;
    }


    /**
     * @brief 返回缓冲区中可写的数据长度
     */
    size_t
    writable() const noexcept {
        return buf ? (core::PKG_MAX_LEN - wpos) : 0;
    }


    /**
     * @brief 向缓冲区尾部追加数据
     * 
     * @return 成功返回 true, 否则返回 false
     */
    bool
    append(const uint8_t* data, uint32_t len) noexcept;


    bool
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
}; // RcvBuf;


} // namespace typhon::core;


#endif // __TYPHON_CORE_BUFFER_HPP__