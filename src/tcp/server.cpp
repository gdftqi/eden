#include "tcp/server.hpp"
#include "tcp/config.hpp"


static constexpr int TIMEOUT = 1000;
static constexpr int RBUF_SIZE = 1500;


void
typhon::tcp::Server::run() noexcept {
    auto expected = core::State::Stopped;
    if (!state_.compare_exchange_strong(expected, core::State::Starting)) {
        return;
    }

    init();
    event_->on_init(this);

    int i, n;
    constexpr size_t MAX_EVENTS = MAX_CONN + 2;
    ::epoll_event events[MAX_EVENTS];

    state_.store(core::State::Running);

    ASSERT(::listen(lfd_, SOMAXCONN) == 0, "监听 TCP fd 失败: errno = {}, errstr = {}", errno, ::strerror(errno));
    while (running()) {
        n = ::epoll_wait(epfd_, events, MAX_EVENTS, TIMEOUT);
        if (n == -1) {
            if (errno == EINTR) {
                continue;
            }
            xERROR("epoll_wait failed: errno = {}, errstr = {}", errno, ::strerror(errno));
            break;
        }

        for (i = 0; i < n; ++i) {
            auto& ev = events[i];
            if (ev.data.fd == stop_evfd_) {
                on_stop_handle(ev);
            } else if (ev.data.fd == lfd_) {
                on_listen_handle(ev);
            } else {
                on_session_handle(ev);
            }
        }
    }

    release();
    event_->on_stopped(this);

    state_.store(core::State::Stopped);
}


void
typhon::tcp::Server::init() noexcept {
    epfd_ = ::epoll_create1(0);
    ASSERT(epfd_ != core::INVALID_SOCKET, "创建 epoll fd 失败: errno = {}, errstr = {}", errno, ::strerror(errno));

    stop_evfd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT(stop_evfd_ != core::INVALID_SOCKET, "创建 停止事件 fd 失败: errno = {}, errstr = {}", errno, ::strerror(errno));

    lfd_ = core::tcp_listen(host_, Conf::instance()->sndbuf(), Conf::instance()->rcvbuf());
    ASSERT(lfd_ != core::INVALID_SOCKET, "创建 TCP 监听 fd 失败: errno = {}, errstr = {}", errno, ::strerror(errno));

    ::epoll_event ev;
    ev.data.fd = stop_evfd_;
    ev.events = EPOLLIN | EPOLLET;
    ASSERT((::epoll_ctl(epfd_, EPOLL_CTL_ADD, stop_evfd_, &ev)) == 0, "errno = {}, errstr = {}", errno, ::strerror(errno));

    ev.data.fd = lfd_;
    ASSERT(::epoll_ctl(epfd_, EPOLL_CTL_ADD, lfd_, &ev) == 0, "errno = {}, errstr = {}", errno, ::strerror(errno));

    int n = std::thread::hardware_concurrency();
    n = n > 2 ? n - 2 : 2;

    for (int i = 0; i < n; ++i) {
        procs_.emplace_back(Proc::create(this, i));
        threads_.emplace_back((std::bind(&Proc::run, procs_[i].get())));
    }
}


void
typhon::tcp::Server::release() noexcept {
    for (auto& w: procs_) {
        w->stop();
    }

    for (auto& t: threads_) {
        t.join();
    }
    
    threads_.clear();
    procs_.clear();

    if (epfd_ != core::INVALID_SOCKET) {
        ::close(epfd_);
        epfd_ = core::INVALID_SOCKET;
    }

    if (stop_evfd_ != core::INVALID_SOCKET) {
        ::close(stop_evfd_);
        stop_evfd_ = core::INVALID_SOCKET;
    }

    if (lfd_ != core::INVALID_SOCKET) {
        ::close(lfd_);
        lfd_ = core::INVALID_SOCKET;
    }

    for (auto& s: sessions_) {
        if (s) {
            event_->on_disconnected(s);
            s = nullptr;
        }
    }
}


void
typhon::tcp::Server::on_stop_handle(const ::epoll_event& ev) noexcept {
    if (ev.events & EPOLLIN) {
        while (1) {
            uint64_t event;
            auto n = ::read(stop_evfd_, &event, sizeof(event));
            if (n == -1) {
                int err = errno;
                if (err == EINTR) {
                    continue;
                }

                if (err != EAGAIN && err != EWOULDBLOCK) {
                    xERROR("read failed: errno = {}, errstr = {}", err, ::strerror(err));
                }
                break;
            }
        }
    }
}


void
typhon::tcp::Server::on_listen_handle(const ::epoll_event& ev) noexcept {
    int err;
    if (ev.events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
        socklen_t len = sizeof(err);
        if (::getsockopt(lfd_, SOL_SOCKET, SO_ERROR, &err, &len)) {
            err = errno;
            xERROR("getsockopt failed: errno = {}, errstr = {}", err, ::strerror(err));
            return;
        }

        if (err) {
            xERROR("listen socket error: errno = {}, errstr = {}", err, ::strerror(err));
            return;
        }
    }

    if (ev.events & EPOLLIN) {
        while (1) {
            core::SOCKET cfd = ::accept4(lfd_, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
            if (cfd < 0) {
                err = errno;
                if (err == EINTR) {
                    continue;
                }

                if (err != EAGAIN && err != EWOULDBLOCK) {
                    xERROR("accept failed: errno = {}, errstr = {}", err, ::strerror(err));
                }

                break;
            }

            ::epoll_event event;
            event.data.fd = cfd;
            event.events = EPOLLIN | EPOLLET | EPOLLOUT;
            ASSERT(::epoll_ctl(epfd_, EPOLL_CTL_ADD, cfd, &event) == 0, "failed: errno = {}, errstr = {}", errno, ::strerror(errno));
            procs_[cfd % procs_.size()]->notify(new core::QEvent(core::QEvent::Type::AddSess, (void*)(uintptr_t)cfd));
        }
    }
}


void
typhon::tcp::Server::on_session_handle(const ::epoll_event& ev) noexcept {
    core::SOCKET fd = ev.data.fd;
    bool del = false;

    if (ev.events & EPOLLIN) {
        int n;
        static uint8_t buf[RBUF_SIZE];

        while (1) {
            n = ::recv(fd, buf, RBUF_SIZE, 0);
            if (n <= 0) {
                if (n < 0) {
                    if (errno == EINTR) {
                        continue;
                    }

                    if (errno != EAGAIN && errno != EWOULDBLOCK) {
                        del = true;
                        xERROR("recv failed: errno = {}, errstr = {}", errno, ::strerror(errno));
                    }
                }
                
                break;
            } else {
                RcvArg* rbuf = (RcvArg*)::mi_malloc(sizeof(RcvArg) + n);
                ASSERT(rbuf != nullptr, "failed to allocate memory for RcvBuf");
                rbuf->len = n;
                rbuf->fd = fd;
                ::memcpy(rbuf->data, buf, n);
                procs_[fd % procs_.size()]->notify(new core::QEvent(core::QEvent::Type::Recv, rbuf));
            }
        }
    }

    if (ev.events & EPOLLOUT) {
        procs_[fd % procs_.size()]->notify(new core::QEvent(core::QEvent::Type::Send, (void*)(uintptr_t)fd));
    }

    if (ev.events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
        del = true;
    }

    if (del) {
        ASSERT(::epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, nullptr) == 0, "failed to remove session from epoll: errno = {}, errstr = {}", errno, ::strerror(errno));
        procs_[fd % procs_.size()]->notify(new core::QEvent(core::QEvent::Type::RmvSess, (void*)(uintptr_t)fd));
    }
}