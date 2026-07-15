#include "core/buffer.hpp"


int
adam::core::Buffer::append(const uint8_t* data, uint32_t len) noexcept {
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