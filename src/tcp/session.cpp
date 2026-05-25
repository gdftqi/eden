#include "tcp/config.hpp"
#include "tcp/proc.hpp"
#include "tcp/session.hpp"


typhon::tcp::Session::Session(core::SOCKET sockfd, Proc* w) noexcept
    : sockfd_(sockfd)
    , addrlen_(sizeof(addr_))
    , last_recv_ms_(w->tnow())
    , proc_(w) {
    static constexpr int on = 1;
    const int sndbuf = Conf::instance()->sndbuf();
    const int rcvbuf = Conf::instance()->rcvbuf();
    ASSERT(::getpeername(sockfd_, (sockaddr*)&addr_, &addrlen_) == 0, "failed to get peer name");
    ASSERT(core::set_nonblocking(sockfd_) == 0, "failed to set non-blocking");
    ASSERT(::setsockopt(sockfd_, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on)) == 0, "failed to set TCP_NODELAY");
    ASSERT(::setsockopt(sockfd_, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf)) == 0, "failed to set send buffer");
    ASSERT(::setsockopt(sockfd_, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf)) == 0, "failed to set receive buffer");
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


int
typhon::tcp::Session::send(core::PackageEx* pke) noexcept {
    if (sbuf_.size() > 0) {
        int n = send(sbuf_.data(), sbuf_.size());
        if (n < 0) {
            return n;
        } else if (n > 0) {
            sbuf_.erase(sbuf_.begin(), sbuf_.begin() + n);
        }
    }

    if (pke == nullptr) {
        return 0;
    }

    int total = pke->pke_len;
    core::pke_hton(pke);
    auto* pk = core::pke_get_pk(pke);
    core::pk_hton(pk);
    uint8_t* buf = (uint8_t*)pke;

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