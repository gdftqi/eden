#include "kcp/server.hpp"


static constexpr int MAX_EVENTS = 2;
static constexpr int TIMEOUT = 10;


typhon::kcp::Server::Server(const char* host, IEvent* ev) noexcept
    : event_(ev)
    , host_(host) {
    ASSERT(host && ev, "invalid host or IEvent instance");

    for (int i = 0; i < MAX_RECV; ++i) {
        auto hdr = &rmsgs_[i].msg_hdr;
        hdr->msg_iov = &riovecs_[i];
        hdr->msg_iovlen = 1;
        hdr->msg_name = &raddrs_[i];
        hdr->msg_namelen = sizeof(raddrs_[i]);

        riovecs_[i].iov_base = ::mi_malloc(core::UDP_MTU);
        ASSERT(riovecs_[i].iov_base != nullptr, "failed to allocate memory for riovec");
        riovecs_[i].iov_len = core::UDP_MTU;
    }

    ufd_ = core::udp_bind(host_);
    ASSERT(ufd_ != core::INVALID_SOCKET, "创建 udp fd 失败: errno = {}, errstr = {}", errno, ::strerror(errno));
}


void
typhon::kcp::Server::run() noexcept {
    auto expected = core::State::Stopped;
    if (!state_.compare_exchange_strong(expected, core::State::Starting)) {
        return;
    }

    init();
    int err = event_->on_init(this);
    if (err) {
        release();
        state_.store(core::State::Stopped);
        return;
    }

    int i, n;
    ::epoll_event events[MAX_EVENTS];
    auto base_ms = (uint32_t)core::systime_ms();

    state_.store(core::State::Running);

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

        tnow_ = (uint32_t)core::systime_ms() - base_ms;
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

    state_.store(core::State::Stopped);
}


int
typhon::kcp::Server::output(const char *buf, int len, IKCPCB*, void *user) noexcept {
    auto* s = (Session*)user;
    s->server()->sque_.emplace_back(
        std::make_unique<core::SndBuf>(s->addr(), s->addrlen(), buf, len, s->server()->tnow())
    );
    return 0;
}


void
typhon::kcp::Server::init() noexcept {
    epfd_ = ::epoll_create1(0);
    ASSERT(epfd_ != core::INVALID_SOCKET, "创建 epoll fd 失败: errno = {}, errstr = {}", errno, ::strerror(errno));

    stop_evfd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT(stop_evfd_ != core::INVALID_SOCKET, "创建 停止事件 fd 失败: errno = {}, errstr = {}", errno, ::strerror(errno));

    ::epoll_event ev;
    ev.data.fd = stop_evfd_;
    ev.events = EPOLLIN | EPOLLET;
    ASSERT(::epoll_ctl(epfd_, EPOLL_CTL_ADD, stop_evfd_, &ev) == 0, "errno = {}, errstr = {}", errno, ::strerror(errno));

    ev.data.fd = ufd_;
    ev.events = EPOLLIN | EPOLLET;
    ASSERT(::epoll_ctl(epfd_, EPOLL_CTL_ADD, ufd_, &ev) == 0, "errno = {}, errstr = {}", errno, ::strerror(errno));
}


void
typhon::kcp::Server::release() noexcept {
    if (epfd_ != core::INVALID_SOCKET) {
        ::close(epfd_);
        epfd_ = core::INVALID_SOCKET;
    }

    if (stop_evfd_ != core::INVALID_SOCKET) {
        ::close(stop_evfd_);
        stop_evfd_ = core::INVALID_SOCKET;
    }

    if (ufd_ != core::INVALID_SOCKET) {
        ::close(ufd_);
        ufd_ = core::INVALID_SOCKET;
    }
}


int
typhon::kcp::Server::on_stop_handle(const ::epoll_event& ev) noexcept {
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
typhon::kcp::Server::on_udp_handle(const ::epoll_event& ev) noexcept {
    thread_local static uint8_t rbuf[core::PKG_MAX_LEN];

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
                riovecs_[i].iov_len = core::UDP_MTU;
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
                auto conv = Session::getconv(hdr->msg_iov[0].iov_base, msg.msg_len);
                if (conv == 0) {
                    continue;
                }

                auto s = get_session(conv);
                if (s == nullptr) {
                    s = Session::create(conv, this, hdr->msg_name, hdr->msg_namelen);
                    if (add_session(conv, s)) {
                        continue;
                    }
                }

                if (s->input(hdr->msg_iov[0].iov_base, msg.msg_len, hdr->msg_name, hdr->msg_namelen)) {
                    continue;
                }

                core::Package* pkg;
                while (true) {
                    int rc = s->recv_pk(&pkg, rbuf, core::PKG_MAX_LEN, tnow_);
                    if (rc <= -1) {
                        if (rc < -1) {
                            remove_session(s->conv());
                        }
                        break;
                    } else if (rc == 0) {
                        continue;
                    }

                    if (event_->on_data(s, pkg) != 0) {
                        remove_session(s->conv());
                        break;
                    }
                }
            }
        } // while(1);
    }

    return res;
}


void
typhon::kcp::Server::update() noexcept {
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

    // 移除超时的消息发送缓冲, 避免一直重试发送一个发不出去的包导致 sque_ 堆积过大占内存
    for (auto itr = sque_.begin(); itr != sque_.end();) {
        auto& buf = *itr;
        if (buf->time + (Session::conf().timeout / 2) < tnow()) {
            itr = sque_.erase(itr);
        } else {
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

            hdr.msg_name = &buf->addr;
            hdr.msg_namelen = buf->addrlen;
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