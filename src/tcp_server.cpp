#include "tcp_server.hpp"


static constexpr int MAX_EVENTS = 1024;
static constexpr int TIMEOUT = 1000;
static constexpr int RBUF_SIZE = 1500;


void
typhon::TcpServer::run() noexcept {
    State expected = State::Stopped;
    if (!state_.compare_exchange_strong(expected, State::Starting)) {
        return;
    }

    init();

    int i, n, err;
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

    state_.store(State::Stopped);
}


void
typhon::TcpServer::init() noexcept {
    epfd_ = ::epoll_create1(0);
    ASSERT(epfd_ != INVALID_SOCKET, "创建 epoll fd 失败: errno = {}, errstr = {}", errno, ::strerror(errno));

    stop_evfd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT(stop_evfd_ != INVALID_SOCKET, "创建 停止事件 fd 失败: errno = {}, errstr = {}", errno, ::strerror(errno));

    lfd_ = tcp_listen(host_);
    ASSERT(lfd_ != INVALID_SOCKET, "创建 TCP 监听 fd 失败: errno = {}, errstr = {}", errno, ::strerror(errno));

    ::epoll_event ev;
    ev.data.fd = stop_evfd_;
    ev.events = EPOLLIN | EPOLLET;
    ASSERT((::epoll_ctl(epfd_, EPOLL_CTL_ADD, stop_evfd_, &ev)) == 0, "errno = {}, errstr = {}", errno, ::strerror(errno));

    ev.data.fd = lfd_;
    ASSERT(::epoll_ctl(epfd_, EPOLL_CTL_ADD, lfd_, &ev) == 0, "errno = {}, errstr = {}", errno, ::strerror(errno));

    uint32_t n = std::thread::hardware_concurrency();
    n = n > 2 ? n - 2 : 2;

    for (uint32_t i = 0; i < n; ++i) {
        workers_.emplace_back(TcpWorker::create(this));
        threads_.emplace_back(std::thread(std::bind(&TcpWorker::run, workers_[i].get())));
    }
}


void
typhon::TcpServer::release() noexcept {
    if (epfd_ != -1) {
        ::close(epfd_);
        epfd_ = -1;
    }

    if (stop_evfd_ != -1) {
        ::close(stop_evfd_);
        stop_evfd_ = -1;
    }

    if (lfd_ != -1) {
        ::close(lfd_);
        lfd_ = -1;
    }

    workers_.clear();
    threads_.clear();

    for (auto& s: sessions_) {
        if (s != nullptr) {
            delete s;
            s = nullptr;
        }
    }
}


int
typhon::TcpServer::on_stop_handle(const ::epoll_event& ev) noexcept {
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
typhon::TcpServer::on_listen_handle(const ::epoll_event& ev) noexcept {
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
            SOCKET cfd = ::accept4(lfd_, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
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
            event.events  = EPOLLIN | EPOLLET;
            ASSERT(::epoll_ctl(epfd_, EPOLL_CTL_ADD, cfd, &event) == 0, "failed: errno = {}, errstr = {}", errno, ::strerror(errno));
            add_session(cfd);
        }
    }

    return res;
}


int
typhon::TcpServer::on_session_handle(const ::epoll_event& ev) noexcept {
    SOCKET fd = ev.data.fd;

    if (ev.events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
        ASSERT(::epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, nullptr) == 0, "errno = {}, errstr = {}", errno, ::strerror(errno));
        remove_session(fd);
        return 0;
    }

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

                ASSERT(::epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, nullptr) == 0, "failed: errno = {}, errstr = {}", errno, ::strerror(errno));
                remove_session(fd);
                break;
            } else {
                RcvBuf* rbuf = (RcvBuf*)::mi_malloc(sizeof(RcvBuf) + n);
                rbuf->len = n;
                rbuf->fd = fd;
                ::memcpy(rbuf->data, buf, n);
                workers_[fd % workers_.size()]->push(rbuf);
            }
        }
    }

    return 0;
}