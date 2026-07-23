#include "tcp/buffer.hpp"


int
adam::tcp::Buffer::append(const uint8_t* data, uint32_t len) noexcept {
    if (writable() < len) {
        compact();

        if (writable() < len) {
            size_t need   = (size_t)wpos + len;
            size_t maxcap = limit ? (size_t)limit : (size_t)UINT32_MAX;
            if (need > maxcap) {
                return xERR;
            }

            size_t ncap = cap ? cap : INIT_SIZE;
            while (ncap < need) {
                ncap <<= 1;
            }
            if (ncap > maxcap) {
                ncap = maxcap;
            }

            auto* nbuf = (uint8_t*)::mi_realloc(buf, ncap);
            ASSERT(nbuf != nullptr, "::mi_realloc 调用失败");
            buf = nbuf;
            cap = (uint32_t)ncap;
        }
    }

    ::memcpy(buf + wpos, data, len);
    wpos += len;
    return xOK;
}