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
            if (ev.data.fd == stop_evfd_) {
                err = on_stop_handle(ev);
            } else if (ev.data.fd == que_evfd_) {
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
    ASSERT(epfd_ != INVALID_SOCKET, "epoll_create1 failed: errno = {}, errstr = {}", errno, ::strerror(errno));

    que_evfd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT(que_evfd_ != INVALID_SOCKET, "eventfd failed: errno = {}, errstr = {}", errno, ::strerror(errno));

    stop_evfd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT(stop_evfd_ != INVALID_SOCKET, "eventfd failed: errno = {}, errstr = {}", errno, ::strerror(errno));

    ::epoll_event ev;
    ev.data.fd = que_evfd_;
    ev.events = EPOLLIN | EPOLLET;
    ASSERT(::epoll_ctl(epfd_, EPOLL_CTL_ADD, que_evfd_, &ev) == 0, "errno = {}, errstr = {}", errno, ::strerror(errno));

    ev.data.fd = stop_evfd_;
    ev.events = EPOLLIN | EPOLLET;
    ASSERT(::epoll_ctl(epfd_, EPOLL_CTL_ADD, stop_evfd_, &ev) == 0, "errno = {}, errstr = {}", errno, ::strerror(errno));
}


void
typhon::TcpWorker::release() noexcept {
    if (epfd_ > 0) {
        ::close(epfd_);
        epfd_ = -1;
    }

    if (que_evfd_ > 0) {
        ::close(que_evfd_);
        que_evfd_ = -1;
    }

    if (stop_evfd_ > 0) {
        ::close(stop_evfd_);
        stop_evfd_ = -1;
    }

    size_t i, n;
    while (!rque_.empty()) {
        RcvBuf* rbufs[16];
        n = rque_.try_dequeue_bulk(rbufs, 16);
        for (i = 0; i < n; ++i) {
            auto& rbuf = rbufs[i];
            // TODO 处理接收数据
            ::mi_free(rbuf);
        }
    }
}


int
typhon::TcpWorker::on_stop_handle(const ::epoll_event& ev) noexcept {
    int err = 0;

    if (ev.events & EPOLLIN) {
        int n;
        while (1) {
            uint64_t event;
            n = ::read(stop_evfd_, &event, sizeof(event));
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
    if (!(ev.events & EPOLLIN)) {
        return 0;
    }

    while (1) {
        uint64_t event;
        auto n = ::read(que_evfd_, &event, sizeof(event));
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            return -errno;
        }
    }

    sending_.store(false, std::memory_order_relaxed);

    size_t i, n;
    while (!rque_.empty()) {
        RcvBuf* rbufs[16];
        n = rque_.try_dequeue_bulk(rbufs, 16);
        for (i = 0; i < n; ++i) {
            auto& rbuf = rbufs[i];
            // TODO 处理接收数据
            ::mi_free(rbuf);
        }
    }

    return 0;
}