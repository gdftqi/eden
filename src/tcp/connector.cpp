#include "tcp/connector.hpp"


ssize_t
typhon::tcp::Connector::send(core::PackageEx* pke) noexcept {
    ssize_t n;

    if (sbuf_.size() > 0) {
        n = core::writen(fd_, sbuf_.data(), sbuf_.size());
        if (n < 0) {
            return n;
        }
        else if (n > 0) {
            sbuf_.erase(sbuf_.begin(), sbuf_.begin() + n);
        }
    }

    if (pke == nullptr) {
        return 0;
    }

    ssize_t total = pke->pke_len;
    core::pke_hton(pke);
    uint8_t* p = (uint8_t*)pke;

    if (sbuf_.size() > 0) {
        sbuf_.insert(sbuf_.end(), p, p + total);
        return 0;
    }

    n = core::writen(fd_, p, total);
    if (n < 0) {
        return n;
    }

    if (n < total) {
        sbuf_.insert(sbuf_.end(), p + n, p + total);
        return 0;
    }

    return 1;
}


int
typhon::tcp::Connector::recv(core::PackageEx** pke, uint64_t now) noexcept {
    if (rbuf_.readable() == 0) {
        return -1;
    }

    if (!rbuf_.decode(pke)) {
        return -2;
    }

    last_recv_ms_ = now;
    return 1;
}