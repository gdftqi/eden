#include "tcp_server.hpp"
#include "utils/log.hpp"


static constexpr int MAX_EVENTS = 1024;
static constexpr int TIMEOUT = 1000;
static constexpr int RBUF_SIZE = 1500;


int
typhon::TcpServer::run() noexcept {
    State expected = State::Stopped;
    if (!state_.compare_exchange_strong(expected, State::Starting)) {
        return -1;
    }

    int err = init();
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

    release();

    state_.store(State::Stopped);
    return err;
}


int
typhon::TcpServer::init() noexcept {
    int err = 0;

    epfd_ = ::epoll_create1(0);
    if (epfd_ == -1) {
        err = -errno;
        return err;
    }

    evfd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (evfd_ == -1) {
        err = -errno;
        release();
        return err;
    }

    lfd_ = tcp_listen(host_);
    if (lfd_ == -1) {
        err = -errno;
        release();
        return err;
    }

    ::epoll_event ev;
    ev.data.fd = evfd_;
    ev.events = EPOLLIN | EPOLLET;
    if (::epoll_ctl(epfd_, EPOLL_CTL_ADD, evfd_, &ev)) {
        err = -errno;
        release();
        return err;
    }

    ev.data.fd = lfd_;
    if (::epoll_ctl(epfd_, EPOLL_CTL_ADD, lfd_, &ev)) {
        err = -errno;
        release();
        return err;
    }

    return 0;
}


void
typhon::TcpServer::release() noexcept {
    if (epfd_ != -1) {
        ::close(epfd_);
        epfd_ = -1;
    }

    if (evfd_ != -1) {
        ::close(evfd_);
        evfd_ = -1;
    }

    if (lfd_ != -1) {
        ::close(lfd_);
        lfd_ = -1;
    }
}


int
typhon::TcpServer::on_event_handle(const ::epoll_event& ev) noexcept {
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
        }
    }

    return res;
}


int
typhon::TcpServer::on_session_handle(const ::epoll_event& ev) noexcept {
    int res = 0;
    SOCKET fd = ev.data.fd;

    if (ev.events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
        socklen_t len = sizeof(res);
        if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &res, &len)) {
            return -errno;
        }

        if (res != 0) {
            return -res;
        }
    }

    if (ev.events & EPOLLIN) {
        int n;

        while (1) {
            RcvBuf* rbuf = (RcvBuf*)::mi_malloc(sizeof(RcvBuf) + RBUF_SIZE);
            n = ::recv(fd, rbuf->data, RBUF_SIZE, 0);
            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }

                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    res = 0;
                    break;
                }

                ASSERT(::epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, nullptr) == 0, "failed: errno = {}, errstr = {}", errno, ::strerror(errno));
                ::close(fd);
            } else if (n == 0) {
                ASSERT(::epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, nullptr) == 0, "failed: errno = {}, errstr = {}", errno, ::strerror(errno));
                ::close(fd);
            } else {
                rbuf->len = n;
                rbuf->fd = fd;
                workers_[fd % workers_.size()]->push(rbuf);
            }
        }
    }

    return res;
}