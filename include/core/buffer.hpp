#ifndef __TYPHON_CORE_BUFFER_HPP__
#define __TYPHON_CORE_BUFFER_HPP__


#include <deque>
#include "core/typhon.in.hpp"
#include "core/package.hpp"
#include "utils/cryptor.hpp"


namespace typhon::core {


struct SndBuf {
    typedef std::deque<SndBuf*> Que;


    ::sockaddr_storage addr;
    ::socklen_t        addrlen;
    uint32_t           len;
    uint64_t           time;
    uint64_t*          siphash;
    uint8_t            buf[UDP_MTU];


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
 * @brief Lazy-heap 接收缓冲。`buf` 初始为 nullptr,直到首次 `append()` 才
 *        `mi_malloc(PKG_MAX_LEN)` 一次性分配满。整个 session 生命周期内
 *        buf 要么是 nullptr(从未收到数据),要么是 PKG_MAX_LEN 大小的堆区。
 *
 * 设计取舍:
 *   - 冷连接 / idle session 完全不占堆,只是几个标量字段。
 *   - 一收到数据就一次性给到 PKG_MAX_LEN,后续不再 realloc,append/decode/compact
 *     的代码路径里没有任何分支判断容量。
 *   - mimalloc 对 64KB 这种粒度的 alloc/free 是 thread-local 快路径,
 *     session 析构时还回去开销可控。
 */
struct RcvBuf {
    uint32_t rpos { 0 };
    uint32_t wpos { 0 };
    uint8_t* buf  { nullptr };       // 首次 append 才 mi_malloc(PKG_MAX_LEN),session 内不再变


    RcvBuf() noexcept = default;


    ~RcvBuf() noexcept {
        if (buf) {
            ::mi_free(buf);
        }
    }


    RcvBuf(const RcvBuf&) = delete;
    RcvBuf& operator=(const RcvBuf&) = delete;
    RcvBuf(RcvBuf&&) = delete;
    RcvBuf& operator=(RcvBuf&&) = delete;


    size_t
    readable() const noexcept {
        return wpos - rpos;
    }


    size_t
    writable() const noexcept {
        // buf 未分配时视为 0(append 会按需 alloc 后再走 writable 检查)
        return buf ? (core::PKG_MAX_LEN - wpos) : 0;
    }


    bool
    append(const uint8_t* data, uint32_t len) noexcept;


    bool
    decode(core::PackageEx** pke) noexcept;


    void
    reset() noexcept {
        rpos = wpos = 0;
    }


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