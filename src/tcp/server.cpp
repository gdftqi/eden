#include "tcp/server.hpp"


static constexpr int MAX_EVENTS = 1024;
static constexpr int TIMEOUT = 1000;
static constexpr int RBUF_SIZE = 1500;


void
typhon::tcp::Server::run() noexcept {
    auto expected = core::State::Stopped;
    if (!state_.compare_exchange_strong(expected, core::State::Starting)) {
        return;
    }

    init();
    if (event_->on_init(this) != 0) {
        release();
        state_.store(core::State::Stopped);
        return;
    }

    int i, n, err;
    ::epoll_event events[MAX_EVENTS];
    auto base_ms = (uint32_t)core::systime_ms();

    state_.store(core::State::Running);

    ASSERT(::listen(lfd_, SOMAXCONN) == 0, "监听 TCP fd 失败: errno = {}, errstr = {}", errno, ::strerror(errno));
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

        tnow_.store((uint32_t)core::systime_ms() - base_ms, std::memory_order_relaxed);
        for (i = 0; i < n; ++i) {
            auto& ev = events[i];
            if (ev.data.fd == stop_evfd_) {
                err = on_stop_handle(ev);
            } else if (ev.data.fd == lfd_) {
                err = on_listen_handle(ev);
            } else {
                err = on_session_handle(ev);
            }
        }

        if (err) {
            break;
        }

        // TODO check heartbeat
    }

    for (auto& w: workers_) {
        w->stop();
    }

    for (auto& t: threads_) {
        t.join();
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

    lfd_ = core::tcp_listen(host_);
    ASSERT(lfd_ != core::INVALID_SOCKET, "创建 TCP 监听 fd 失败: errno = {}, errstr = {}", errno, ::strerror(errno));

    ::epoll_event ev;
    ev.data.fd = stop_evfd_;
    ev.events = EPOLLIN | EPOLLET;
    ASSERT((::epoll_ctl(epfd_, EPOLL_CTL_ADD, stop_evfd_, &ev)) == 0, "errno = {}, errstr = {}", errno, ::strerror(errno));

    ev.data.fd = lfd_;
    ASSERT(::epoll_ctl(epfd_, EPOLL_CTL_ADD, lfd_, &ev) == 0, "errno = {}, errstr = {}", errno, ::strerror(errno));

    uint32_t n = std::thread::hardware_concurrency();
    n = n > 2 ? n - 2 : 2;

    for (uint32_t i = 0; i < n; ++i) {
        workers_.emplace_back(Worker::create(this));
        threads_.emplace_back(std::thread(std::bind(&Worker::run, workers_[i].get())));
    }
}


void
typhon::tcp::Server::release() noexcept {
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

    workers_.clear();
    threads_.clear();
    for (auto& s: sessions_) {
        s = nullptr;
    }
}


int
typhon::tcp::Server::on_stop_handle(const ::epoll_event& ev) noexcept {
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
typhon::tcp::Server::on_listen_handle(const ::epoll_event& ev) noexcept {
    int res = 0;

    if (ev.events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
        socklen_t len = sizeof(res);
        if (::getsockopt(lfd_, SOL_SOCKET, SO_ERROR, &res, &len)) {
            return -errno;
        }

        if (res != 0) {
            return -res;
        }
    }

    if (ev.events & EPOLLIN) {
        while (1) {
            core::SOCKET cfd = ::accept4(lfd_, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
            if (cfd < 0) {
                int err = errno;
                if (err == EINTR) {
                    continue;
                }

                if (err == EAGAIN || err == EWOULDBLOCK) {
                    res = 0;
                } else {
                    res = -err;
                }

                break;
            }

            ::epoll_event event;
            event.data.fd = cfd;
            event.events = EPOLLIN | EPOLLET | EPOLLOUT;
            ASSERT(::epoll_ctl(epfd_, EPOLL_CTL_ADD, cfd, &event) == 0, "failed: errno = {}, errstr = {}", errno, ::strerror(errno));
            workers_[cfd % workers_.size()]->push(new QEvent(QEvent::Type::AddSess, (void*)(uintptr_t)cfd));
        }
    }

    return res;
}


int
typhon::tcp::Server::on_session_handle(const ::epoll_event& ev) noexcept {
    core::SOCKET fd = ev.data.fd;
    bool del = false;

    if (ev.events & EPOLLIN) {
        int n;
        thread_local static uint8_t buf[RBUF_SIZE];

        while (1) {
            n = ::recv(fd, buf, RBUF_SIZE, 0);
            if (n <= 0) {
                if (n < 0) {
                    if (errno == EINTR) {
                        continue;
                    }

                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        break;
                    }

                    xERROR("{} recv 失败: errno = {}, errstr = {}", fd, errno, ::strerror(errno));
                }

                del = true;
                break;
            } else {
                RcvArg* rbuf = (RcvArg*)::mi_malloc(sizeof(RcvArg) + n);
                ASSERT(rbuf != nullptr, "failed to allocate memory for RcvBuf");
                rbuf->len = n;
                rbuf->fd = fd;
                ::memcpy(rbuf->data, buf, n);
                workers_[fd % workers_.size()]->push(new QEvent(QEvent::Type::Recv, rbuf));
            }
        }
    }

    if (ev.events & EPOLLOUT) {
        workers_[fd % workers_.size()]->push(new QEvent(QEvent::Type::Send, (void*)(uintptr_t)fd));
    }

    if (ev.events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
        del = true;
    }

    if (del) {
        workers_[fd % workers_.size()]->push(new QEvent(QEvent::Type::RmvSess, (void*)(uintptr_t)fd));
    }

    return 0;
}