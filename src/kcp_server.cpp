#include "kcp_server.hpp"

#include <netdb.h>
#include <string.h>
#include <sys/eventfd.h>

#include "typhon.in.hpp"
#include "package.hpp"


static constexpr int MAX_EVENTS = 2;
static constexpr int TIMEOUT = 10;


typhon::KcpServer::KcpServer(const char* host, IEvent* ev)
    : event_(ev)
    , host_(host) {
    if (ev == nullptr) {
        throw new std::runtime_error("IEvent is invalid");
    }

    for (int i = 0; i < MAX_RECV; ++i) {
        auto hdr = &rmsgs_[i].msg_hdr;
        hdr->msg_iov = &riovecs_[i];
        hdr->msg_iovlen = 1;
        hdr->msg_name = &raddrs_[i];
        hdr->msg_namelen = sizeof(raddrs_[i]);

        riovecs_[i].iov_base = ::mi_malloc(UDP_MTU);
        riovecs_[i].iov_len = UDP_MTU;
    }

    sque_.reserve(MAX_RECV * 4);

    // 在 ctor 里 bind，让 sockfd_ 在 run() 之前就可用——给 BpfRouter 注册用
    sockfd_ = udp_bind(host_);
}


int
typhon::KcpServer::run() noexcept {
    auto expected = State::Stopped;
    if (!state_.compare_exchange_strong(expected, State::Starting)) {
        return -1;
    }

    int err = init();
    if (err) {
        state_.store(State::Stopped);
        return err;
    }

    err = event_->on_init(this);
    if (err) {
        state_.store(State::Stopped);
        return err;
    }

    int i, n;
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
    event_->on_stopped(this);

    state_.store(State::Stopped);
    return err;
}


int
typhon::KcpServer::output(const char *buf, int len, IKCPCB*, void *user) noexcept {
    auto kcp = ((Kcp*)user)->shared_from_this();
    kcp->server()->sque_.emplace_back(SendBuf::create(kcp, buf, len));
    return 0;
}


int
typhon::KcpServer::init() noexcept {
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

    // sockfd_ 已经在 ctor 里 bind 过；这里只验证有效
    if (sockfd_ == INVALID_SOCKET) {
        return -EINVAL;
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
typhon::KcpServer::release() noexcept {
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
typhon::KcpServer::on_event_handle(const ::epoll_event& ev) noexcept {
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
typhon::KcpServer::on_udp_handle(const ::epoll_event& ev) noexcept {
    thread_local static uint8_t rbuf[PACK_MAX_LEN];

    int i, n, err;

    if (!(ev.events & EPOLLIN) && !(ev.events & EPOLLERR)) {
        return -EINVAL;
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
            auto conv = Kcp::getconv(hdr->msg_iov[0].iov_base, msg.msg_len);
            if (conv == 0) {
                continue;
            }

            auto kcp = get_session(conv);
            if (kcp == nullptr) {
                kcp = Kcp::create(conv, this);
                add_session(conv, kcp);
            }

            if (kcp->input(hdr->msg_iov[0].iov_base, msg.msg_len, hdr->msg_name, hdr->msg_namelen)) {
                continue;
            }

            Package* pkg;
            while (true) {
                int res = kcp->recv_pk(&pkg, rbuf, PACK_MAX_LEN, tnow_);
                if (res < 0) {
                    continue;
                }

                if (res == 0) {
                    break;
                }

                if (event_->on_data(kcp, pkg) != 0) {
                    remove_session(kcp->conv());
                }
            }
        }
    }

    return err;
}


void
typhon::KcpServer::update() noexcept {
    for (auto itr = sessions_.begin(); itr != sessions_.end();) {
        auto s = itr->second;
        if (s->timeout(tnow_)) {
            itr = sessions_.erase(itr);
            event_->on_disconnected(s);
            continue;
        } else {
            s->update(tnow_);
            ++itr;
        }
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

            hdr.msg_name = buf->kcp->addr();
            hdr.msg_namelen = buf->kcp->addrlen();
            hdr.msg_iov = &iovecs[i];
            hdr.msg_iovlen = 1;

            iovecs[i].iov_base = buf->buf;
            iovecs[i].iov_len = buf->len;
        }

        int res = ::sendmmsg(sockfd_, msgs, n, 0);
        if (res == -1) {
            break;
        }

        nsnd += res;
        if (res < n) {
            break;
        }
    }

    sque_.erase(sque_.begin(), sque_.begin() + nsnd);
}