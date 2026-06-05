#include "tcp/connector.hpp"


ssize_t
typhon::tcp::Connector::send(core::PackageEx* pke, uint64_t now) noexcept {
    ssize_t n;

    if (sbuf_.size() > 0) {
        n = core::writen(fd_, sbuf_.data(), sbuf_.size());
        if (n < 0) {
            return n;
        }
        else if (n > 0) {
            last_send_ms_ = now;
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

    last_send_ms_ = now;
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


int
typhon::tcp::Connector::update(uint64_t now) noexcept {
    static uint64_t timeout = 0;
    if (timeout == 0) {
        timeout = Conf::instance()->timeout() / 3;
    }

    if (is_connected()) {
        if (now - last_recv_ms_ > (uint64_t)Conf::instance()->timeout()) {
            return -1;
        }

        if (now - last_send_ms_ > timeout) {
            uint8_t buf[core::PKG_HDR_EX_LEN + core::PKG_HDR_LEN] = {0};
            core::PackageEx* pke = (core::PackageEx*)buf;
            auto* pk = core::pke_get_pk(pke);
            pk->pk_dst_id = id_;
            pk->pk_id = PKID_PING;
            pke->pke_len = sizeof(buf);
            if (send(pke, now) < 0) {
                return -1;
            }
        }
    }

    return 0;
}