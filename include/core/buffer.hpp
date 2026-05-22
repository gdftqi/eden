#ifndef __TYPHON_CORE_BUFFER_HPP__
#define __TYPHON_CORE_BUFFER_HPP__


#include <deque>
#include "core/typhon.in.hpp"
#include "core/package.hpp"


namespace typhon::core {


struct SndBuf {
    typedef std::unique_ptr<SndBuf> Ptr;
    typedef std::deque<SndBuf::Ptr> Que;


    ::sockaddr_storage addr;
    ::socklen_t        addrlen;
    uint32_t           len;
    uint32_t           time;
    uint8_t            buf[UDP_MTU];


    SndBuf(const void* addr, ::socklen_t addrlen, const char* b, uint32_t l, uint32_t time) noexcept 
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
    append(const uint8_t* data, uint32_t len) noexcept {
        if (!buf) {
            buf = (uint8_t*)::mi_malloc(core::PKG_MAX_LEN);
            if (!buf) {
                return false;
            }
        }

        if (writable() < len) {
            compact();
            if (writable() < len) {
                return false;          // readable + len > PKG_MAX_LEN,装不下了
            }
        }

        ::memcpy(buf + wpos, data, len);
        wpos += len;
        return true;
    }


    bool
    decode(core::Package** pkg, core::PackageTail** pkt) noexcept {
        // Lazy compact: rpos 越过 buf 中线时把 readable 区回搬到 buf 起始,
        // 把代价分摊到 decode 路径,避免 append 在 wpos 顶满时被迫做大块 memmove。
        // 这一步安全的前提是:调用方在拿到新 *pkg 之前,上一次 decode 返回的 *pkg
        // 必然已经消费完,搬动数据不会让任何在用指针失效。
        if (rpos > core::PKG_MAX_LEN / 2) {
            compact();
        }

        if (readable() < core::PKG_HEADER_LEN) {
            return false;
        }

        auto* p = (core::Package*)(buf + rpos);
        uint16_t pklen = ::ntohs(p->pk_len);

        ASSERT(pklen >= core::PKG_HEADER_LEN + core::PKG_TAIL_LEN, "invalid package length: {}", pklen);

        if (readable() < pklen) {
            return false;
        }

        pk_ntoh(p);
        auto* t = (core::PackageTail*)(buf + rpos + pklen - core::PKG_TAIL_LEN);
        pkt_ntoh(t);
        *pkg = p;
        *pkt = t;
        rpos += pklen;
        return true;
    }


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