#include "core/buffer.hpp"


bool
typhon::core::RcvBuf::append(const uint8_t* data, uint32_t len) noexcept {
    if (!buf) {
        buf = (uint8_t*)::mi_malloc(core::PKG_MAX_LEN);
        if (!buf) {
            return false;
        }
    }

    if (writable() < len) {
        compact();
        if (writable() < len) {
            return false;
        }
    }

    ::memcpy(buf + wpos, data, len);
    wpos += len;
    return true;
}


bool
typhon::core::RcvBuf::decode(core::PackageEx** pkx) noexcept {
    if (rpos > core::PKG_MAX_LEN / 2) {
        compact();
    }

    if (readable() < core::PKX_HDR_LEN) {
        return false;
    }

    auto* p = (core::PackageEx*)(buf + rpos);
    uint16_t pkxlen = ::ntohs(p->pke_len);

    ASSERT(pkxlen >= core::PKX_HDR_LEN + core::PKG_HDR_LEN, "invalid package ex length: {}", pkxlen);

    if (readable() < pkxlen) {
        return false;
    }

    ntoh(PKx<Net>(p));
    *pkx = p;
    rpos += pkxlen;
    return true;
}