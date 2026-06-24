#include "core/buffer.hpp"


int
typhon::core::RcvBuf::append(const uint8_t* data, uint32_t len) noexcept {
    if (writable() < len) {
        compact();

        if (writable() < len) {
            size_t need = (size_t)wpos + len;
            if (need > core::RCVBUF_MAX) {
                return xERR;
            }

            uint32_t ncap = cap ? cap : core::RCVBUF_INIT;
            while (ncap < need) {
                ncap <<= 1;
            }
            if (ncap > core::RCVBUF_MAX) {
                ncap = core::RCVBUF_MAX;
            }

            auto* nbuf = (uint8_t*)::mi_realloc(buf, ncap);
            ASSERT(nbuf != nullptr, "::mi_realloc 调用失败");
            buf = nbuf;
            cap = ncap;
        }
    }

    ::memcpy(buf + wpos, data, len);
    wpos += len;
    return xOK;
}


int
typhon::core::RcvBuf::decode(core::PackageEx** pkx) noexcept {
    if (readable() < core::PKX_HDR_LEN) {
        return xAGAIN;
    }

    auto* p = (core::PackageEx*)(buf + rpos);
    uint16_t pkxlen = ::ntohs(p->len);

    ASSERT(pkxlen >= core::PKX_HDR_LEN + core::PKG_HDR_LEN, "invalid package ex length: {}", pkxlen);

    if (readable() < pkxlen) {
        return xAGAIN;
    }

    ntoh(PKx<Net>(p));
    // *pkx 指向 buf 内部(零拷贝), 仅在下次 append/compact 前有效。
    // 调用方持有它期间不得对本 RcvBuf 调 append/compact, 否则 use-after-realloc。详见 buffer.hpp。
    *pkx = p;
    rpos += pkxlen;
    return xOK;
}