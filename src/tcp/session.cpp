#include "tcp/config.hpp"
#include "tcp/proc.hpp"
#include "tcp/session.hpp"


typhon::tcp::Session::Session(core::SOCKET sockfd, Proc* w) noexcept
    : fd_(sockfd)
    , addrlen_(sizeof(addr_))
    , last_recv_ms_(w->tnow())
    , proc_(w) {
    static constexpr int on = 1;
    const int sndbuf = Conf::instance()->sndbuf();
    const int rcvbuf = Conf::instance()->rcvbuf();
    ASSERT(::getpeername(fd_, (sockaddr*)&addr_, &addrlen_) == 0, "failed to get peer name");
    ASSERT(core::set_nonblocking(fd_) == 0, "failed to set non-blocking");
    ASSERT(::setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on)) == 0, "failed to set TCP_NODELAY");
    ASSERT(::setsockopt(fd_, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf)) == 0, "failed to set send buffer");
    ASSERT(::setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf)) == 0, "failed to set receive buffer");
}


int
typhon::tcp::Session::recv(core::PackageEx** pke) noexcept {
    if (rbuf_.readable() == 0) {
        return -1;
    }

    if (!rbuf_.decode(pke)) {
        return -2;
    }

    last_recv_ms_ = proc_->tnow();
    return 1;
}


ssize_t
typhon::tcp::Session::send(core::PackageEx* pke) noexcept {
    ssize_t n;

    if (sbuf_.size() > 0) {
        n = core::writen(fd_, sbuf_.data(), sbuf_.size());
        if (n < 0) {
            return n;
        } else if (n > 0) {
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