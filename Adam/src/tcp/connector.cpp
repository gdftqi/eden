#include "tcp/connector.hpp"
#include "core/error.hpp"
#include "kcp/config.hpp"
#include <cstring>


ssize_t
adam::tcp::Connector::send(uint64_t now) noexcept {
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
adam::tcp::Connector::send(core::Package& pk, uint64_t now) noexcept {
    // 先把排队残留续发出去(保序)
    ssize_t n = send(now);
    if (n < 0) {
        return n;
    }

    // 完整帧(meta + data, 小端)序列化到暂存 buf
    thread_local static uint8_t buf[core::PKG_MAX_LEN];
    int total = core::frame_encode(buf, &pk);

    if (sbuf_.readable() > 0) {
        // 前面还有排队 → 保序追加, 等 EPOLLOUT 续发; append 撞硬顶返回 xERR(背压 → 上抛判死)
        return sbuf_.append(buf, (uint32_t)total);
    }

    n = core::writen(fd_, buf, total);
    if (n < 0) {
        return n;
    }

    last_send_ms_ = now;
    if (n < total) {
        // 部分写, 余下入 sbuf_(同样可能撞硬顶 → 判死)
        return sbuf_.append(buf + n, (uint32_t)(total - n));
    }

    return xOK;
}


int
adam::tcp::Connector::recv(core::Package* pk, uint64_t now) noexcept {
    // Buffer 只存字节; 分帧(读 len)+ 解码都在 frame_decode 里, Buffer 不知道帧格式
    int n = core::frame_decode(pk, rbuf_.peek(), rbuf_.readable());
    if (n == 0) {
        return xAGAIN;   // 半包, 等更多数据
    }
    if (n < 0) {
        return xERR;     // 帧非法
    }

    rbuf_.consume((uint32_t)n);
    last_recv_ms_ = now;
    return xOK;
}


int
adam::tcp::Connector::update(uint64_t now) noexcept {
    static uint64_t timeout = 0;
    if (timeout == 0) {
        timeout = kcp::Conf::instance()->server()->timeout / 3;
    }

    if (!is_connected()) {
        return xOK;
    }

    if (now - last_recv_ms_ > kcp::Conf::instance()->server()->timeout) {
        // 接收超时, 判死
        return xERR;
    }

    // 心跳: 仅注册确认(authed)后才发。payload = now(8B, 原样回显, 字节序无关)
    if (authed_ && now - last_send_ms_ > timeout) {
        alignas(core::Package) uint8_t buf[sizeof(core::Package) + sizeof(uint64_t)] = {0};
        auto* pk = (core::Package*)buf;
        pk->meta.len = core::PKG_HDR_LEN + sizeof(uint64_t);
        pk->data.pid  = PID_PING;
        ::memcpy(pk->data.payload, &now, sizeof(now));

        if (send(*pk, now) < 0) {
            return xERR;
        }
    }

    return xOK;
}


int
adam::tcp::Connector::regist(uint32_t id, uint64_t now) noexcept {
    if (!is_connected()) {
        return xOK;
    }

    // REGIST_REQ: payload = 服务 id(4B 小端)。后端 on_regist 需按小端读。
    alignas(core::Package) uint8_t buf[sizeof(core::Package) + sizeof(uint32_t)] = {0};
    auto* pk = (core::Package*)buf;
    pk->meta.len = core::PKG_HDR_LEN + sizeof(uint32_t);
    pk->data.pid  = PID_REG_BKD_REQ;

    uint32_t v = core::u32_to_le(id);
    ::memcpy(pk->data.payload, &v, sizeof(v));

    if (send(*pk, now) < 0) {
        return xERR;
    }

    return xOK;
}
