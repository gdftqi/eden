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


    ::sockaddr_storage addr;               ///< 对端地址
    ::socklen_t        addrlen;            ///< 地址长度
    uint32_t           len;                ///< wire 数据报长度(含 8B SipHash 信封, 由 xkcp 加好)
    uint64_t           time;               ///< 缓冲区创建的时间
    uint8_t            buf[XKCP_LINK_MTU]; ///< 缓冲区(直接存 xkcp 产出的完整 wire 数据报)


    explicit
    SndBuf(const void* addr, ::socklen_t addrlen, const uint8_t* b, uint32_t l, uint64_t time) noexcept
        : addrlen(addrlen)
        , len(l)
        , time(time) {
        ::memcpy(&this->addr, addr, addrlen);
        ASSERT(l <= XKCP_LINK_MTU, "len = {}, max = {}", l, XKCP_LINK_MTU);
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
struct RcvBuf {
    uint32_t rpos { 0 };       ///< 读游标
    uint32_t wpos { 0 };       ///< 写游标
    uint32_t cap  { 0 };       ///< 当前已分配容量(0 = 未分配)
    uint8_t* buf  { nullptr }; ///< 缓冲区


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
     * @warning **生命周期契约**: 成功时 *pkx 指向本 RcvBuf 内部 buf(零拷贝),
     *          仅在下一次 append()/compact() 之前有效 —— append 可能触发
     *          mi_realloc(换地址) 或 compact 的 memmove(数据前移), 都会让 *pkx 悬垂。
     *          约束: **持有 decode 出来的 *pkx 期间, 绝不能对同一 RcvBuf 调 append/compact**。
     *          典型用法是"先把本轮可读数据全 append 进来, 再循环 decode 逐个处理完",
     *          循环内不再 append, 即满足此约束。
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