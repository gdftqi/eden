#include "tcp/config.hpp"
#include "tcp/proc.hpp"
#include "tcp/session.hpp"
#include "core/error.hpp"


adam::tcp::Session::Session(sSOCKET sockfd, Proc* w) noexcept
    : fd_(sockfd)
    , addrlen_(sizeof(addr_))
    , last_recv_ms_(w->tnow())
    , proc_(w) {
    constexpr int on = 1;
    ASSERT(::getpeername(fd_, (sockaddr*)&addr_, &addrlen_) == 0, "failed to get peer name");
    ASSERT(core::set_nonblocking(fd_) == 0, "failed to set non-blocking");
    ASSERT(::setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on)) == 0, "failed to set TCP_NODELAY");
}


int
adam::tcp::Session::recv(core::Package* pk) noexcept {
    // Buffer 只存字节; 分帧(读 len)+ 解码都在 frame_decode 里
    int n = core::frame_decode(pk, rbuf_.peek(), rbuf_.readable());
    if (n == 0) {
        return xAGAIN;   // 半包, 等更多数据
    }
    if (n < 0) {
        return xERR;     // 帧非法
    }

    rbuf_.consume((uint32_t)n);
    last_recv_ms_ = proc_->tnow();
    return xOK;
}


// 纯 flush: 把 sbuf_ 积压字节尽量写出(EPOLLOUT 续发用)
ssize_t
adam::tcp::Session::send() noexcept {
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


// 发一个包: 先 flush 残留(保序), 再整帧(meta+data, 小端)序列化写出; 部分写存 sbuf_。
ssize_t
adam::tcp::Session::send(core::Package& pk) noexcept {
    ssize_t n = send();
    if (n < 0) {
        return n;
    }

    thread_local static uint8_t buf[core::PKG_MAX_LEN];
    ssize_t total = core::frame_encode(buf, &pk);

    if (sbuf_.size() > 0) {
        sbuf_.insert(sbuf_.end(), buf, buf + total);
        return xOK;
    }

    n = core::writen(fd_, buf, total);
    if (n < 0) {
        return n;
    }

    if (n < total) {
        sbuf_.insert(sbuf_.end(), buf + n, buf + total);
        return xOK;
    }

    return xOK;
}