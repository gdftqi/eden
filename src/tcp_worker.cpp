#include "tcp_worker.hpp"


void
typhon::TcpWorker::run() noexcept {
    auto expected = State::Stopped;
    if (!state_.compare_exchange_strong(expected, State::Starting)) {
        return;
    }

    init();

    int i, n, err;
    ::epoll_event events[2];

    state_.store(State::Running);

    while (running()) {
        err = 0;
        n = ::epoll_wait(epfd_, events, 2, -1);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            err = -errno;
            break;
        }

        for (i = 0; i < n; ++i) {
            auto& ev = events[i];
            if (ev.data.fd == s_evfd_) {
                err = on_stop_handle(ev);
            } else if (ev.data.fd == q_evfd_) {
                err = on_que_handle(ev);
            }
        }

        if (err) {
            break;
        }

        // TODO 发送数据
    }

    release();
    state_.store(State::Stopped);
}


void
typhon::TcpWorker::init() noexcept {
    epfd_ = ::epoll_create1(0);
    ASSERT(epfd_ > 0, "epoll_create1 failed: errno = {}, errstr = {}", errno, ::strerror(errno));

    q_evfd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT(q_evfd_ > 0, "eventfd failed: errno = {}, errstr = {}", errno, ::strerror(errno));

    s_evfd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT(s_evfd_ > 0, "eventfd failed: errno = {}, errstr = {}", errno, ::strerror(errno));
}


void
typhon::TcpWorker::release() noexcept {
    if (epfd_ > 0) {
        ::close(epfd_);
        epfd_ = -1;
    }

    if (q_evfd_ > 0) {
        ::close(q_evfd_);
        q_evfd_ = -1;
    }

    if (s_evfd_ > 0) {
        ::close(s_evfd_);
        s_evfd_ = -1;
    }
}


int
typhon::TcpWorker::on_stop_handle(const ::epoll_event& ev) noexcept {
    int err = 0;

    if (ev.events & EPOLLIN) {
        int n;
        while (1) {
            uint64_t event;
            n = ::read(s_evfd_, &event, sizeof(event));
            if (n < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    err = -errno;
                }
                break;
            }
        }
    }

    return err;
}


int
typhon::TcpWorker::on_que_handle(const ::epoll_event& ev) noexcept {
    return 0;
}