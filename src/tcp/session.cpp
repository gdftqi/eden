#include "tcp/config.hpp"
#include "tcp/proc.hpp"
#include "tcp/session.hpp"
#include "core/error.hpp"


typhon::tcp::Session::Session(core::SOCKET sockfd, Proc* w) noexcept
    : fd_(sockfd)
    , addrlen_(sizeof(addr_))
    , last_recv_ms_(w->tnow())
    , proc_(w) {
    constexpr int on = 1;
    const int sndbuf = Conf::instance()->sndbuf();
    const int rcvbuf = Conf::instance()->rcvbuf();
    ASSERT(::getpeername(fd_, (sockaddr*)&addr_, &addrlen_) == 0, "failed to get peer name");
    ASSERT(core::set_nonblocking(fd_) == 0, "failed to set non-blocking");
    ASSERT(::setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on)) == 0, "failed to set TCP_NODELAY");
    ASSERT(::setsockopt(fd_, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf)) == 0, "failed to set send buffer");
    ASSERT(::setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf)) == 0, "failed to set receive buffer");
}


int
typhon::tcp::Session::recv(core::PKx<core::Host>* pke) noexcept {
    if (rbuf_.readable() == 0) {
        return xAGAIN;
    }

    core::PackageEx* raw;
    if (rbuf_.decode(&raw) != xOK) {           // decode 内部已 ntoh → host 序
        return xAGAIN;                   // 半包, 等更多数据
    }

    *pke = core::PKx<core::Host>(raw);
    last_recv_ms_ = proc_->tnow();
    return xOK;
}


// 纯 flush: 把 sbuf_ 积压字节尽量写出(EPOLLOUT 续发用)
ssize_t
typhon::tcp::Session::send() noexcept {
    if (sbuf_.empty()) {
        return xOK;
    }

    ssize_t n = core::writen(fd_, sbuf_.data(), sbuf_.size());
    if (n < 0) {
        return n;
    }

    if (n > 0) {
        sbuf_.erase(sbuf_.begin(), sbuf_.begin() + n);
    }
    
    return xOK;
}


// 发一个包: 先 flush 残留(保序), 再 hton 成网络序写出; 部分写存 sbuf_。
ssize_t
typhon::tcp::Session::send(core::PKx<core::Host> pkx) noexcept {
    ssize_t n = send();
    if (n < 0) {
        return n;
    }

    ssize_t  total = pkx->len;
    auto     net   = core::hton(pkx);
    uint8_t* p     = net.raw();

    if (sbuf_.size() > 0) {
        sbuf_.insert(sbuf_.end(), p, p + total);
        return xOK;
    }

    n = core::writen(fd_, p, total);
    if (n < 0) {
        return n;
    }

    if (n < total) {
        sbuf_.insert(sbuf_.end(), p + n, p + total);
        return xOK;
    }
    
    return xOK;
}