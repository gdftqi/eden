#include "tcp/connector.hpp"
#include "core/error.hpp"
#include "kcp/config.hpp"


ssize_t
typhon::tcp::Connector::send(uint64_t now) noexcept {
    size_t pending = sbuf_.readable();
    if (pending == 0) {
        return xOK;
    }

    ssize_t n = core::writen(fd_, sbuf_.peek(), pending);
    if (n < 0) {
        return n;
    }

    if (n > 0) {
        last_send_ms_ = now;
        sbuf_.consume((uint32_t)n);
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

    if (sbuf_.readable() > 0) {
        // 前面还有排队中的残留 → 保序追加, 等 EPOLLOUT 续发。
        // append 满 RCVBUF_MAX 返回 xERR: 积压到硬顶即背压, 上抛判死重连。
        return sbuf_.append(p, (uint32_t)total);
    }

    n = core::writen(fd_, p, total);
    if (n < 0) {
        return n;
    }

    last_send_ms_ = now;
    if (n < total) {
        // 部分写, 余下存 sbuf_(同样可能撞 RCVBUF_MAX → 判死)
        return sbuf_.append(p + n, (uint32_t)(total - n));
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
        // 半包, 等更多数据
        return xAGAIN;
    }

    *pkx = core::PKx<core::Host>(raw, raw->len);
    last_recv_ms_ = now;
    return xOK;
}


int
typhon::tcp::Connector::update(uint64_t now) noexcept {
    constexpr int BUF_SIZE = core::PKX_HDR_LEN + core::PKG_HDR_LEN + sizeof(uint64_t);

    static uint64_t timeout = 0;

    if (timeout == 0) {
        timeout = kcp::Conf::instance()->timeout() / 3;
    }

    if (is_connected()) {
        if (now - last_recv_ms_ > (uint64_t)kcp::Conf::instance()->timeout()) {
            // 接收超时, 判死
            return xERR;
        }

        // 心跳: 仅注册确认(authed)后才发。
        if (authed_ && now - last_send_ms_ > timeout) {
            uint8_t buf[BUF_SIZE] = {0};
            
            core::PKx<core::Host> pkx(buf, BUF_SIZE);
            pkx->len     = BUF_SIZE;
            pkx.pk()->id = PKID_PING;
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
    constexpr int BUF_SIZE = core::PKX_HDR_LEN + core::PKG_HDR_LEN + sizeof(id);

    if (is_connected()) {
        uint8_t buf[BUF_SIZE] = {0};
        core::PKx<core::Host> pkx(buf, BUF_SIZE);
        pkx->len = BUF_SIZE;
        pkx.pk()->id = PKID_REGIST_REQ;

        (*(uint32_t*)pkx.pk()->payload) = htonl(id);
        if (send(pkx, now) < 0) {
            return xERR;
        }
    }

    return xOK;
}