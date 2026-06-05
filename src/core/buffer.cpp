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
typhon::core::RcvBuf::decode(core::PackageEx** pke) noexcept {
    if (rpos > core::PKG_MAX_LEN / 2) {
        compact();
    }

    if (readable() < core::PKG_HDR_EX_LEN) {
        return false;
    }

    auto* p = (core::PackageEx*)(buf + rpos);
    uint16_t pklen = ::ntohs(p->pke_len);

    ASSERT(pklen >= core::PKG_HDR_EX_LEN, "invalid package ex length: {}", pklen);

    if (readable() < pklen) {
        return false;
    }

    ntoh(Pke<Net>(p));   // net → host: 外层 + 内嵌各翻一次(替代旧的三重翻转)
    *pke = p;
    rpos += pklen;
    return true;
}