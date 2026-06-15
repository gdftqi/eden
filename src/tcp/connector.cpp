#include "tcp/connector.hpp"
#include "core/error.hpp"


ssize_t
typhon::tcp::Connector::send(uint64_t now) noexcept {
    if (sbuf_.empty()) {
        return xOK;
    }

    ssize_t n = core::writen(fd_, sbuf_.data(), sbuf_.size());
    if (n < 0) {
        return n;
    }

    if (n > 0) {
        last_send_ms_ = now;
        sbuf_.erase(sbuf_.begin(), sbuf_.begin() + n);
    }

    return xOK;
}


ssize_t
typhon::tcp::Connector::send(core::PKx<core::Host> pkx, uint64_t now) noexcept {
    ssize_t n = send(now);
    if (n < 0) {
        return n;
    }

    ssize_t  total = pkx->len;
    auto     net   = core::hton(pkx);
    uint8_t* p     = net.raw();

    if (sbuf_.size() > 0) {
        sbuf_.insert(sbuf_.end(), p, p + total);
        return xOK;                  // 排队保序, 等 EPOLLOUT 续发
    }

    n = core::writen(fd_, p, total);
    if (n < 0) {
        return n;
    }

    last_send_ms_ = now;
    if (n < total) {
        sbuf_.insert(sbuf_.end(), p + n, p + total);
        return xOK;                  // 部分写, 余下存 sbuf_
    }

    return xOK;
}


int
typhon::tcp::Connector::recv(core::PKx<core::Host>* pkx, uint64_t now) noexcept {
    if (rbuf_.readable() == 0) {
        return xAGAIN;
    }

    core::PackageEx* raw;
    if (rbuf_.decode(&raw) != xOK) {
        return xAGAIN;               // 半包, 等更多数据
    }

    *pkx = core::PKx<core::Host>(raw);
    last_recv_ms_ = now;
    return xOK;
}


int
typhon::tcp::Connector::update(uint64_t now) noexcept {
    static uint64_t timeout = 0;
    if (timeout == 0) {
        timeout = Conf::instance()->timeout() / 3;
    }

    if (is_connected()) {
        if (now - last_recv_ms_ > (uint64_t)Conf::instance()->timeout()) {
            return xERR;             // 接收超时, 判死
        }

        // 心跳: 仅注册确认(authed)后才发。
        if (authed_ && now - last_send_ms_ > timeout) {
            constexpr int BUF_SIZE = core::PKX_HDR_LEN + core::PKG_HDR_LEN + sizeof(uint64_t);
            uint8_t buf[BUF_SIZE] = {0};
            core::PKx<core::Host> pkx{ buf };
            pkx->len        = BUF_SIZE;
            pkx.pk()->id     = PKID_PING;
            (*(uint64_t*)pkx.pk()->payload) = now;
            if (send(pkx, now) < 0) {
                return xERR;
            }
        }
    }

    return xOK;
}


int
typhon::tcp::Connector::regist(uint32_t id, uint64_t now) noexcept {
    if (is_connected()) {
        constexpr int BUF_SIZE = core::PKX_HDR_LEN + core::PKG_HDR_LEN + sizeof(id);
        uint8_t buf[BUF_SIZE] = {0};
        core::PKx<core::Host> pkx{ buf };
        pkx->len = BUF_SIZE;
        pkx.pk()->id = PKID_REGIST_REQ;

        (*(uint32_t*)pkx.pk()->payload) = htonl(id);
        if (send(pkx, now) < 0) {
            return xERR;
        }
    }

    return xOK;
}