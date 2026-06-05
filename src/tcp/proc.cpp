#include "tcp/config.hpp"
#include "tcp/server.hpp"
#include "tcp/proc.hpp"


void
typhon::tcp::Proc::run() noexcept {
    auto expected = core::State::Stopped;
    if (!state_.compare_exchange_strong(expected, core::State::Starting)) {
        return;
    }

    init();

    int i, n;
    ::epoll_event events[2];

    state_.store(core::State::Running);

    while (running()) {
        n = ::epoll_wait(epfd_, events, 2, 10);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            xERROR("epoll_wait failed: errno = {}, errstr = {}", errno, ::strerror(errno));
            break;
        }

        tnow_ = core::systime_ms();

        for (i = 0; i < n; ++i) {
            auto& ev = events[i];
            if (ev.data.fd == evfd_) {
                on_event_handle(ev);
            }
        }

        if (last_check_ms_ + 1000 < tnow_) {
            check_timeout();
            last_check_ms_ = tnow_;
        }
    }

    release();
    state_.store(core::State::Stopped);
}


void
typhon::tcp::Proc::init() noexcept {
    epfd_ = ::epoll_create1(0);
    ASSERT(epfd_ != core::INVALID_SOCKET, "epoll_create1 failed: errno = {}, errstr = {}", errno, ::strerror(errno));

    evfd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT(evfd_ != core::INVALID_SOCKET, "eventfd failed: errno = {}, errstr = {}", errno, ::strerror(errno));

    ::epoll_event ev;
    ev.data.fd = evfd_;
    ev.events = EPOLLIN | EPOLLET;
    ASSERT(::epoll_ctl(epfd_, EPOLL_CTL_ADD, evfd_, &ev) == 0, "errno = {}, errstr = {}", errno, ::strerror(errno));
}


void
typhon::tcp::Proc::release() noexcept {
    if (epfd_ != core::INVALID_SOCKET) {
        ::close(epfd_);
        epfd_ = core::INVALID_SOCKET;
    }

    if (evfd_ != core::INVALID_SOCKET) {
        ::close(evfd_);
        evfd_ = core::INVALID_SOCKET;
    }

    size_t i, n;
    core::QEvent* qes[16];
    while ((n = evque_.try_dequeue_bulk(qes, 16)) > 0) {
        for (i = 0; i < n; ++i) {
            auto& qe = qes[i];
            if (qe->qe_type == core::QEvent::Type::Recv) {
                auto* rbuf = (core::RecvArg*)qe->qe_data;
                ::mi_free(rbuf);
            }
            delete qe;
        }
    }
}


void
typhon::tcp::Proc::on_event_handle(const ::epoll_event& ev) noexcept {
    if (!(ev.events & EPOLLIN)) {
        return;
    }

    int err = 0;
    while (1) {
        uint64_t event;
        auto n = ::read(evfd_, &event, sizeof(event));
        if (n < 0) {
            err = errno;
            if (err == EINTR) {
                continue;
            }

            if (err != EAGAIN && err != EWOULDBLOCK) {
                xERROR("read failed: errno = {}, errstr = {}", err, ::strerror(err));
            }
            break;
        }
    }

    evflag_.store(false, std::memory_order_relaxed);

    size_t i, n;
    core::QEvent* qes[16];
    while ((n = evque_.try_dequeue_bulk(qes, 16)) > 0) {
        for (i = 0; i < n; ++i) {
            switch (qes[i]->qe_type) {
            case core::QEvent::Type::Stop:
                // nothing to do
                break;

            case core::QEvent::Type::Recv:
                on_recv_handle(qes[i]);
                break;

            case core::QEvent::Type::Send:
                on_send_handle(qes[i]);
                break;

            case core::QEvent::Type::AddSess:
                on_add_sess_handle(qes[i]);
                break;

            case core::QEvent::Type::RmvSess:
                on_rmv_sess_handle(qes[i]);
                break;

            default:
                break;
            }

            delete qes[i];
        }
    }
}


void
typhon::tcp::Proc::on_recv_handle(core::QEvent* qe) noexcept {
    auto* rbuf = (core::RecvArg*)qe->qe_data;
    auto sess = server_->get_session(rbuf->fd);
    if (sess == nullptr) {
        ::mi_free(rbuf);
        return;
    }

    if (!sess->input(rbuf->data, rbuf->len)) {
        ::mi_free(rbuf);
        return;
    }

    int res;
    core::PackageEx* pke;
    core::Package* pk;
    while (1) {
        res = sess->recv(&pke);
        if (res < 0) {
            break;
        }

        pk = core::pke_get_pk(pke);
        if (pk->pk_id == PKID_PING) {
            pk->pk_id = PKID_PONG;
            sess->send(pke);
            continue;
        }

        auto handler = server_->get_handler(pk->pk_id);
        if (!handler) {
            xWARN("no handler for pk_id {}, from {}", pk->pk_id, sess->remote_addr());
            continue;
        }
        handler(sess, pke);
    }

    ::mi_free(rbuf);
}


void
typhon::tcp::Proc::on_send_handle(core::QEvent* qe) noexcept {
    auto fd = (core::SOCKET)(uintptr_t)qe->qe_data;
    auto sess = server_->get_session(fd);
    if (sess != nullptr) {
        int n = sess->send();
        if (n < 0) {
            xWARN("failed to send data to fd {}, err = {}", fd, -n);
        }
    }
}


void
typhon::tcp::Proc::on_add_sess_handle(core::QEvent* qe) noexcept {
    auto fd = (core::SOCKET)(uintptr_t)qe->qe_data;
    server_->add_session(fd, this);
}


void
typhon::tcp::Proc::on_rmv_sess_handle(core::QEvent* qe) noexcept {
    auto fd = (core::SOCKET)(uintptr_t)qe->qe_data;
    server_->remove_session(fd, false);
}


void
typhon::tcp::Proc::check_timeout() noexcept {
    const auto tn = tnow_;
    const auto timeout = Conf::instance()->timeout();
    const int  n  = Server::MAX_CONN;
    const int  ws = server_->worker_size();

    auto* ss = server_->sessions();
    for (int i = id_; i < n; i += ws) {
        auto& s = ss[i];
        if (!s) {
            continue;
        }

        if (s->last_recv_ms() + (uint64_t)timeout < tn) {
            server_->remove_session(s->sockfd());
        }
    }
}