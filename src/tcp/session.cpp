#include "tcp/session.hpp"


int
typhon::tcp::Session::send(core::Package* pk) noexcept {
    if (sbuf_.size() > 0) {
        int n = send(sbuf_.data(), sbuf_.size());
        if (n < 0) {
            return n;
        } else if (n > 0) {
            sbuf_.erase(sbuf_.begin(), sbuf_.begin() + n);
        }
    }

    if (pk == nullptr) {
        return 0;
    }

    int total = pk->pk_len;
    if (total > core::PKG_MAX_LEN - core::PKG_TAIL_LEN) {
        return -1;
    }

    core::pk_hton(pk);
    uint8_t* buf = (uint8_t*)pk;

    if (sbuf_.size() > 0) {
        sbuf_.insert(sbuf_.end(), buf, buf + total);
        return 0;
    }
    
    int n = send(buf, total);
    if (n < 0) {
        return n;
    }
    
    if (n < total) {
        sbuf_.insert(sbuf_.end(), buf + n, buf + total);
        return 0;
    }

    return 1;
}


int
typhon::tcp::Session::send(const uint8_t* data, size_t len) noexcept {
    int err = 0;
    size_t nleft = len;
    const uint8_t* buf = data;
    while (nleft > 0) {
        int n = ::send(sockfd_, buf, nleft, 0);
        if (n < 0) {
            err = errno;
            if (err == EINTR) {
                continue;
            } else if (err == EAGAIN || err == EWOULDBLOCK) {
                break;
            } else {
                return -err;
            }
        } else if (n == 0) {
            break;
        }

        buf += n;
        nleft -= n;
    }

    return len - nleft;
}