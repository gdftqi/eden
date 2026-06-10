#include "tcp/connector.hpp"


ssize_t
typhon::tcp::Connector::send(uint64_t now) noexcept {
    if (sbuf_.empty()) {
        return 0;
    }

    ssize_t n = core::writen(fd_, sbuf_.data(), sbuf_.size());
    if (n < 0) {
        return n;
    }

    if (n > 0) {
        last_send_ms_ = now;
        sbuf_.erase(sbuf_.begin(), sbuf_.begin() + n);
    }

    return 0;
}


ssize_t
typhon::tcp::Connector::send(core::PKx<core::Host> pke, uint64_t now) noexcept {
    ssize_t n = send(now);
    if (n < 0) {
        return n;
    }

    ssize_t  total = pke->x_len;
    auto     net   = core::hton(pke);
    uint8_t* p     = (uint8_t*)net.raw();

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
typhon::tcp::Connector::recv(core::PKx<core::Host>* pke, uint64_t now) noexcept {
    if (rbuf_.readable() == 0) {
        return -1;
    }

    core::PackageEx* raw;
    if (!rbuf_.decode(&raw)) {
        return -2;
    }

    *pke = core::PKx<core::Host>(raw);
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

        // 心跳: 仅注册确认(authed)后才发。
        if (authed_ && now - last_send_ms_ > timeout) {
            constexpr int BUF_SIZE = core::PKX_HDR_LEN + core::PKG_HDR_LEN + sizeof(uint64_t);
            uint8_t buf[BUF_SIZE] = {0};
            core::PKx<core::Host> pkx{ buf };
            pkx->x_len        = BUF_SIZE;
            pkx.pk()->p_id     = PKID_PING;
            (*(uint64_t*)pkx.pk()->p_payload) = now;
            if (send(pkx, now) < 0) {
                return -1;
            }
        }
    }

    return 0;
}


int
typhon::tcp::Connector::regist(uint32_t id, uint64_t now) noexcept {
    if (is_connected()) {
        constexpr int BUF_SIZE = core::PKX_HDR_LEN + core::PKG_HDR_LEN + sizeof(id);
        uint8_t buf[BUF_SIZE] = {0};
        core::PKx<core::Host> pkx{ buf };
        pkx->x_len = BUF_SIZE;
        pkx.pk()->p_id = PKID_REGIST_REQ;

        (*(uint32_t*)pkx.pk()->p_payload) = htonl(id);
        if (send(pkx, now) < 0) {
            return -1;
        }
    }

    return 0;
}