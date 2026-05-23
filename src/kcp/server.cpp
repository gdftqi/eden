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
    event_->on_init(this);

    int i, n;
    ::epoll_event events[MAX_EVENTS];
    auto base_ms = (uint32_t)core::systime_ms();

    state_.store(core::State::Running);

    while (running()) {
        n = ::epoll_wait(epfd_, events, MAX_EVENTS, TIMEOUT);
        if (n == -1) {
            if (errno == EINTR) {
                continue;
            }
            xERROR("epoll_wait failed: errno = {}, errstr = {}", errno, ::strerror(errno));
            break;
        }

        tnow_ = (uint32_t)core::systime_ms() - base_ms;
        for (i = 0; i < n; ++i) {
            auto& ev = events[i];
            if (ev.data.fd == stop_evfd_) {
                on_stop_handle(ev);
            } else if (ev.data.fd == ufd_) {
                on_udp_handle(ev);
            }
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
    auto svr = s->server();
    auto sb = svr->sb_pool_.acquire(s->addr(), s->addrlen(), buf, len, svr->tnow());
    svr->sque_.emplace_back(sb);
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
    // 取消 ET 模式, 目的是加强防御. 
    // 因为攻击者有可能大量发包, 使用 recvmmsg 不停的读取数据从而导致DOS.
    // 所以作法是只能取有限数量的包, 如果还有未读数据则会被再次唤醒.
    ev.events = EPOLLIN; 
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

    for (auto& [_, s]: sessions_) {
        event_->on_disconnected(s);
    }
    sessions_.clear();

    for (auto* sb: sque_) {
        sb_pool_.release(sb);
    }
    sque_.clear();

    tnow_ = 0;
}


void
typhon::kcp::Server::on_stop_handle(const ::epoll_event& ev) noexcept {
    if (ev.events & EPOLLIN) {
        uint64_t event;
        while (1) {
            auto n = ::read(stop_evfd_, &event, sizeof(event));
            if (n == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                }
            }
        }
    } // if (ev.events & EPOLLIN);
}


void
typhon::kcp::Server::on_udp_handle(const ::epoll_event& ev) noexcept {
    thread_local static uint8_t rbuf[core::PKG_MAX_LEN];

    int res = 0;
    if (ev.events & EPOLLERR) {
        socklen_t len = sizeof(res);
        ASSERT(::getsockopt(ufd_, SOL_SOCKET, SO_ERROR, &res, &len) == 0, "errno = {}, errstr = {}", errno, ::strerror(errno));
        if (res != 0) {
            xERROR("socket error: errno = {}, errstr = {}", res, ::strerror(res));
            return;
        }
    }
    
    if (ev.events & EPOLLIN) {
        int i, n;
        constexpr int MAX_ROUND = 8;
        int round = 0;
        while (round++ < MAX_ROUND) {
            for (i = 0; i < MAX_RECV; ++i) {
                riovecs_[i].iov_len = core::UDP_MTU;
            }

            n = ::recvmmsg(ufd_, rmsgs_, MAX_RECV, MSG_DONTWAIT, nullptr);
            if (n <= 0) {
                if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                    xERROR("recvmsg error: errno = {}, errstr = {}", errno, ::strerror(errno));
                }
                break;
            }

            for (i = 0; i < n; ++i) {
                if (rmsgs_[i].msg_hdr.msg_flags & MSG_TRUNC) {
                    xWARN("UDP truncated from {} dropped", core::sockaddr_to_string((sockaddr*)rmsgs_[i].msg_hdr.msg_name));
                    continue;
                }

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
                    if (rc < -1) {
                        // 读取消息出错
                        remove_session(s->conv());
                        break;
                    } else if (rc == -1) {
                        // 没有消息了
                        break;
                    } else if (rc == 0) {
                        // 幂等错误, 还有数据
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
    size_t exp = 0;
    auto timeout = Session::conf().timeout / 2;
    while (exp < sque_.size() && sque_[exp]->time + timeout < tnow()) {
        sb_pool_.release(sque_[exp]);
        ++exp;
    }
    sque_.erase(sque_.begin(), sque_.begin() + exp);

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
        if (res < 0) {
            xERROR("sendmmsg error: errno = {}, errstr = {}", errno, ::strerror(errno));
            break;
        }

        nsnd += res;
        if (res < n) {
            break;
        }
    }

    for (int i = 0; i < nsnd; ++i) {
        sb_pool_.release(sque_[i]);
    }
    sque_.erase(sque_.begin(), sque_.begin() + nsnd);
}