#include "udp_server.hpp"

#include <netdb.h>
#include <string.h>
#include <sys/eventfd.h>

#include "typhon.in.hpp"


static typhon::SOCKET
udp_bind(const std::string& host) noexcept {
    if (host.empty()) {
        return typhon::INVALID_SOCKET;
    }

    auto pos = host.find(':');
    if (pos == std::string::npos) {
        return typhon::INVALID_SOCKET;
    }

    auto ip = host.substr(0, pos);
    auto port = host.substr(pos + 1);

    if (ip.empty()) {
        ip = "0.0.0.0";
    }

    if (port.empty()) {
        return typhon::INVALID_SOCKET;
    }

    auto fd = typhon::INVALID_SOCKET;
    ::addrinfo hints;
    ::addrinfo *result = nullptr, *rp = nullptr;
    ::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;

    if (::getaddrinfo(ip.c_str(), port.c_str(), &hints, &result) != 0) {
        return typhon::INVALID_SOCKET;
    }

    for (rp = result; rp != nullptr; rp = rp->ai_next) {
        fd = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd == typhon::INVALID_SOCKET) {
            continue;
        }

        static constexpr int reuseport = 1;
        static constexpr int sndbuf = 1024 * 1024 * 4;
        static constexpr int rcvbuf = sndbuf * 2;
        if (typhon::set_nonblocking(fd) ||
            ::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &reuseport, sizeof(reuseport)) ||
            ::setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf)) ||
            ::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf))) {
            ::close(fd);
            continue;
        }

        if (::bind(fd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }

        ::close(fd);
    }

    ::freeaddrinfo(result);

    if (rp == nullptr) {
        return typhon::INVALID_SOCKET;
    }

    return fd;
}


int
typhon::UdpServer::run() noexcept {
    auto expected = State::Stopped;
    if (!state_.compare_exchange_strong(expected, State::Starting)) {
        return -1;
    }

    int err = init();
    if (err) {
        state_.store(State::Stopped);
        return err;
    }

    int i, n;
    constexpr int MAX_EVENTS = 2;
    constexpr int TIMEOUT = 10;
    ::epoll_event events[MAX_EVENTS];
    auto base_ms = (uint32_t)systime_ms();

    state_.store(State::Running);

    while (running()) {
        err = 0;
        n = ::epoll_wait(epfd_, events, MAX_EVENTS, TIMEOUT);
        if (n == -1) {
            if (errno == EINTR) {
                continue;
            }
            err = -errno;
            break;
        }

        tnow_ = (uint32_t)systime_ms() - base_ms;
        for (i = 0; i < n; ++i) {
            auto& ev = events[i];
            if (ev.data.fd == evfd_) {
                err = on_event_handle(ev);
            } else if (ev.data.fd == sockfd_) {
                err = on_udp_handle(ev);
            } else {
                err = -EINVAL;
                break;
            }
        }

        if (err) {
            break;
        }

        update();
    }

    release();

    state_.store(State::Stopped);
    return err;
}


int
typhon::UdpServer::init() noexcept {
    int err = 0;

    epfd_ = ::epoll_create1(0);
    if (epfd_ == -1) {
        err = -errno;
        return err;
    }

    evfd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (evfd_ == -1) {
        err = -errno;
        return err;
    }

    sockfd_ = udp_bind(host_);
    if (sockfd_ == -1) {
        err = -errno;
        return err;
    }

    ::epoll_event ev;
    ev.data.fd = evfd_;
    ev.events = EPOLLIN | EPOLLET;
    if (::epoll_ctl(epfd_, EPOLL_CTL_ADD, evfd_, &ev)) {
        err = -errno;
        return err;
    }

    ev.data.fd = sockfd_;
    ev.events = EPOLLIN | EPOLLET;
    if (::epoll_ctl(epfd_, EPOLL_CTL_ADD, sockfd_, &ev)) {
        err = -errno;
        return err;
    }

    return 0;
}


void
typhon::UdpServer::release() noexcept {
    if (epfd_ != -1) {
        ::close(epfd_);
        epfd_ = -1;
    }

    if (evfd_ != -1) {
        ::close(evfd_);
        evfd_ = -1;
    }

    if (sockfd_ != -1) {
        ::close(sockfd_);
        sockfd_ = -1;
    }
}


int
typhon::UdpServer::on_event_handle(const ::epoll_event& ev) noexcept {
    if (ev.events & EPOLLIN) {
        while (1) {
            uint64_t event;
            auto n = ::read(evfd_, &event, sizeof(event));
            if (n == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                }
                return -errno;
            } else if (n == 0) {
                break;
            } else if (n != sizeof(event)) {
                return -EINVAL;
            }
        }
    }

    return 0;
}


int
typhon::UdpServer::on_udp_handle(const ::epoll_event& ev) noexcept {
    int i, n, err;

    if (!(ev.events & EPOLLIN) && !(ev.events & EPOLLERR)) {
        err = -errno;
        return err;
    }

    while (1) {
        for (i = 0; i < MAX_RECV; ++i) {
            riovecs_[i].iov_len = UDP_MTU;
        }

        err = 0;
        n = ::recvmmsg(sockfd_, rmsgs_, MAX_RECV, MSG_DONTWAIT, nullptr);
        if (n == -1) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                err = -errno;
            }
            break;
        } else if (n == 0) {
            break;
        }

        for (i = 0; i < n; ++i) {
            auto& msg = rmsgs_[i];
            auto hdr = &rmsgs_[i].msg_hdr;
            hdr->msg_iov[0].iov_len = msg.msg_len;
            auto conv = Kcp::getconv(hdr->msg_iov[0].iov_base, msg.msg_len);
            if (conv == 0) {
                continue;
            }

            auto kcp = get_session(conv);
            if (kcp == nullptr) {
                kcp = Kcp::create(conv, hdr->msg_name, hdr->msg_namelen);
                add_session(conv, kcp);
            }

            if (kcp->input(hdr->msg_iov[0].iov_base, msg.msg_len)) {
                remove_session(conv);
                continue;
            }

            auto rbuf = (uint8_t*)::malloc(1024 * 8);            
            auto len = kcp->recv(rbuf, 1024 * 8);
            if (len < -1) {
                remove_session(conv);
                continue;
            }

            // TODO: 事件 读
            kcp->send(rbuf, len);
            ::free(rbuf);
        }
    }

    return err;
}


void
typhon::UdpServer::update() noexcept {
    for (auto& kv : sessions_) {
        auto kcp = kv.second;
        kcp->update(tnow_);
    }

    ::mmsghdr msgs[MAX_SEND] {};
    ::iovec iovecs[MAX_SEND] {};
    int nsnd = 0, total = sque_.size();

    while (nsnd < total) {
        int n = total - nsnd;
        n = n > MAX_SEND ? MAX_SEND : n;

        for (int i = 0; i < n; ++i) {
            auto& buf = sque_[nsnd + i];
            auto& msg = msgs[i];
            auto& hdr = msg.msg_hdr;

            hdr.msg_name = buf.kcp->addr();
            hdr.msg_namelen = *buf.kcp->addrlen();
            hdr.msg_iov = &iovecs[i];
            hdr.msg_iovlen = 1;

            iovecs[i].iov_base = buf.buf;
            iovecs[i].iov_len = buf.len;
        }

        int res = ::sendmmsg(sockfd_, msgs, n, 0);
        if (res == -1) {
            break;
        }

        nsnd += n;
    }

    sque_.erase(sque_.begin(), sque_.begin() + nsnd);
}