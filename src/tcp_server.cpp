#include "tcp_server.hpp"


static constexpr int MAX_EVENTS = 1024;
static constexpr int TIMEOUT = 1000;


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