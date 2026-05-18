#include "kcp_server.hpp"


static constexpr int MAX_EVENTS = 2;
static constexpr int TIMEOUT = 10;


typhon::KcpServer::KcpServer(const char* host, IEvent* ev) noexcept
    : event_(ev)
    , host_(host) {
    ASSERT(host && ev, "invalid host or IEvent instance");

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

    ufd_ = udp_bind(host_);
    ASSERT(ufd_ != INVALID_SOCKET, "创建 udp fd 失败: errno = {}, errstr = {}", errno, ::strerror(errno));
}


void
typhon::KcpServer::run() noexcept {
    auto expected = State::Stopped;
    if (!state_.compare_exchange_strong(expected, State::Starting)) {
        return;
    }

    init();

    int err = event_->on_init(this);
    if (err) {
        state_.store(State::Stopped);
        return;
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
            if (ev.data.fd == stop_evfd_) {
                err = on_stop_handle(ev);
            } else if (ev.data.fd == ufd_) {
                err = on_udp_handle(ev);
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
}


int
typhon::KcpServer::output(const char *buf, int len, IKCPCB*, void *user) noexcept {
    auto kcp = ((Kcp*)user)->shared_from_this();
    kcp->server()->sque_.emplace_back(SendBuf::create(kcp, buf, len));
    return 0;
}


void
typhon::KcpServer::init() noexcept {
    epfd_ = ::epoll_create1(0);
    ASSERT(epfd_ != INVALID_SOCKET, "创建 epoll fd 失败: errno = {}, errstr = {}", errno, ::strerror(errno));

    stop_evfd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT(stop_evfd_ != INVALID_SOCKET, "创建 停止事件 fd 失败: errno = {}, errstr = {}", errno, ::strerror(errno));

    ::epoll_event ev;
    ev.data.fd = stop_evfd_;
    ev.events = EPOLLIN | EPOLLET;
    ASSERT(::epoll_ctl(epfd_, EPOLL_CTL_ADD, stop_evfd_, &ev) == 0, "errno = {}, errstr = {}", errno, ::strerror(errno));

    ev.data.fd = ufd_;
    ev.events = EPOLLIN | EPOLLET;
    ASSERT(::epoll_ctl(epfd_, EPOLL_CTL_ADD, ufd_, &ev) == 0, "errno = {}, errstr = {}", errno, ::strerror(errno));
}


void
typhon::KcpServer::release() noexcept {
    if (epfd_ != -1) {
        ::close(epfd_);
        epfd_ = -1;
    }

    if (stop_evfd_ != -1) {
        ::close(stop_evfd_);
        stop_evfd_ = -1;
    }

    if (ufd_ != -1) {
        ::close(ufd_);
        ufd_ = -1;
    }
}


int
typhon::KcpServer::on_stop_handle(const ::epoll_event& ev) noexcept {
    if (ev.events & EPOLLIN) {
        while (1) {
            uint64_t event;
            auto n = ::read(stop_evfd_, &event, sizeof(event));
            if (n == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                }
                return -errno;
            }
        }
    }

    return 0;
}


int
typhon::KcpServer::on_udp_handle(const ::epoll_event& ev) noexcept {
    thread_local static uint8_t rbuf[PACK_MAX_LEN];

    int res = 0;
    if (ev.events & EPOLLERR) {
        socklen_t len = sizeof(res);
        if (::getsockopt(ufd_, SOL_SOCKET, SO_ERROR, &res, &len)) {
            return -errno;
        }

        if (res != 0) {
            return -res;
        }
    }
    
    if (ev.events & EPOLLIN) {
        int i, n;
        while (1) {
            for (i = 0; i < MAX_RECV; ++i) {
                riovecs_[i].iov_len = UDP_MTU;
            }

            res = 0;
            n = ::recvmmsg(ufd_, rmsgs_, MAX_RECV, MSG_DONTWAIT, nullptr);
            if (n == -1) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    res = -errno;
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
                    kcp = Kcp::create(conv, this, hdr->msg_name, hdr->msg_namelen);
                    add_session(conv, kcp);
                }

                if (kcp->input(hdr->msg_iov[0].iov_base, msg.msg_len, hdr->msg_name, hdr->msg_namelen)) {
                    continue;
                }

                Package* pkg;
                while (true) {
                    int rc = kcp->recv_pk(&pkg, rbuf, PACK_MAX_LEN, tnow_);
                    if (rc < 0) {
                        continue;
                    }

                    if (rc == 0) {
                        break;
                    }

                    if (event_->on_data(kcp, pkg) != 0) {
                        remove_session(kcp->conv());
                        break;
                    }
                }
            }
        } // while(1);
    }

    return res;
}


void
typhon::KcpServer::update() noexcept {
    for (auto itr = sessions_.begin(); itr != sessions_.end();) {
        auto s = itr->second;
        if (s->check_timeout(tnow_)) {
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

        int res = ::sendmmsg(ufd_, msgs, n, 0);
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