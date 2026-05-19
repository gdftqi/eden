#include "tcp_worker.hpp"
#include "tcp_server.hpp"


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
    if (epfd_ != INVALID_SOCKET) {
        ::close(epfd_);
        epfd_ = INVALID_SOCKET;
    }

    if (que_evfd_ != INVALID_SOCKET) {
        ::close(que_evfd_);
        que_evfd_ = INVALID_SOCKET;
    }

    if (stop_evfd_ != INVALID_SOCKET) {
        ::close(stop_evfd_);
        stop_evfd_ = INVALID_SOCKET;
    }

    size_t i, n;
    QEvent* qes[16];
    while ((n = rque_.try_dequeue_bulk(qes, 16)) > 0) {
        for (i = 0; i < n; ++i) {
            auto& qe = qes[i];
            if (qe->qe_type == QEvent::Type::Recv) {
                auto* rbuf = (RcvBuf*)qe->qe_data;
                ::mi_free(rbuf);
            }
            delete qe;
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
     QEvent* qes[16];
    while ((n = rque_.try_dequeue_bulk(qes, 16)) > 0) {
        for (i = 0; i < n; ++i) {
            switch (qes[i]->qe_type) {
            case QEvent::Type::Recv:
                on_qe_recv_handle(qes[i]);
                break;

            case QEvent::Type::AddSess:
                on_qe_add_sess_handle(qes[i]);
                break;

            case QEvent::Type::RmvSess:
                on_qe_rmv_sess_handle(qes[i]);
                break;

            default:
                break;
            }

            delete qes[i];
        }
    }

    return 0;
}


int
typhon::TcpWorker::on_qe_recv_handle(QEvent* qe) noexcept {
    auto* rbuf = (RcvBuf*)qe->qe_data;
    auto* sess = server_->get_session(rbuf->fd);
    ASSERT(sess != nullptr, "session not found for fd: {}", rbuf->fd);
    if (!sess->input(rbuf->data, rbuf->len)) {
        ::mi_free(rbuf);
        return 0;
    }

    int res;
    Package* pk;
    PackageTail* pkt;
    while (1) {
        res = sess->recv(&pk, &pkt, server_->tnow());
        if (res < 0) {
            break;
        } else if (res == 0) {
            continue;
        }

        auto handler = server_->get_handler(pk->pk_id);
        if (!handler) {
            xWARN("no handler for pk_id {}, from {}", pk->pk_id, sess->remote_addr());
            continue;
        }

        handler(sess, pk);
    }

    ::mi_free(rbuf);
    return 0;
}


int
typhon::TcpWorker::on_qe_add_sess_handle(QEvent* qe) noexcept {
    auto fd = (SOCKET)(uintptr_t)qe->qe_data;
    server_->add_session(fd);
    return 0;
}


int
typhon::TcpWorker::on_qe_rmv_sess_handle(QEvent* qe) noexcept {
    auto fd = (SOCKET)(uintptr_t)qe->qe_data;
    server_->remove_session(fd);
    return 0;
}