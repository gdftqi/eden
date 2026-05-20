#include "tcp/session.hpp"


int
typhon::tcp::Session::send(core::Package* pk) noexcept {
    ASSERT(pk != nullptr, "pk 不能为空");

    int total = pk->pk_len;
    if (total > core::PKG_MAX_LEN - core::PKG_TAIL_LEN) {
        return -1;
    }

    uint8_t* buf = (uint8_t*)pk;
    int nleft = pk->pk_len;
    core::pk_hton((core::Package*)pk);
    
    while (nleft > 0) {
        int n = ::send(sockfd_, buf, nleft, 0);
        if (n <= 0) {
            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }

                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    ::_mm_pause();
                    continue;
                } else {
                    return -errno;
                }
            } else {
                return -1;
            }
        }

        buf += n;
        nleft -= n;
    }

    return total - nleft;
}