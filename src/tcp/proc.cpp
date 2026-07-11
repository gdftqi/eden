#include "tcp/config.hpp"
#include "tcp/server.hpp"
#include "tcp/proc.hpp"
#include "core/error.hpp"


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

        tnow_ = utils::systime_ms();

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

    evque_.clear([](core::QEvent* qe) {
        if (qe->qe_type == core::QEvent::Type::Recv) {
            auto* rbuf = (core::RcvArg*)qe->qe_data.ptr;
            ::mi_free(rbuf);
        }
        delete qe;
    });
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
    auto* rbuf = (core::RcvArg*)qe->qe_data.ptr;
    auto sess = server_->get_session(rbuf->fd);
    if (sess == nullptr) {
        ::mi_free(rbuf);
        return;
    }

    if (sess->input(rbuf->data, rbuf->len) != xOK) {
        ::mi_free(rbuf);
        return;
    }

    int res;
    core::PKx<core::Host> pkx;
    while (1) {
        res = sess->recv(&pkx);
        if (res != xOK) {
            break;
        }

        switch (pkx.pk()->id) {
        case PKID_PING:
            on_ping(sess, pkx);
            break;

        case PKID_REGIST_REQ:
            on_regist(sess, pkx);
            break;

        default:
            on_handle(sess, pkx);
            break;
        }
    }

    ::mi_free(rbuf);
}


void
typhon::tcp::Proc::on_send_handle(core::QEvent* qe) noexcept {
    auto fd = (core::SOCKET)(uintptr_t)qe->qe_data.ptr;
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
    auto fd = (core::SOCKET)(uintptr_t)qe->qe_data.ptr;
    server_->add_session(fd, this);
}


void
typhon::tcp::Proc::on_rmv_sess_handle(core::QEvent* qe) noexcept {
    auto fd = (core::SOCKET)(uintptr_t)qe->qe_data.ptr;
    server_->remove_session(fd, false);
}


void
typhon::tcp::Proc::check_timeout() noexcept {
    const auto tn = tnow_;
    const auto to = Conf::instance()->server()->timeout;
    const int  n  = Server::MAX_CONN;
    const int  ws = server_->worker_size();

    auto* ss = server_->sessions();
    for (int i = id_; i < n; i += ws) {
        auto& s = ss[i];
        if (!s) {
            continue;
        }

        if (s->last_recv_ms() + (uint64_t)to < tn) {
            server_->remove_session(s->sockfd());
        }
    }
}


void
typhon::tcp::Proc::on_ping(Session::Ptr s, core::PKx<core::Host> pkx) noexcept {
    pkx.pk()->id = PKID_PONG;
    if (s->send(pkx) < 0) {
        xERROR("发送消息失败");
    }
}


void
typhon::tcp::Proc::on_regist(Session::Ptr s, core::PKx<core::Host> pkx) noexcept {
    uint32_t id = ntohl(*(uint32_t*)pkx.pk()->payload);

    pkx.pk()->id = PKID_REGIST_RSP;
    *(uint32_t*)pkx.pk()->payload = htonl(0);

    s->set_id(id);

    if (s->send(pkx) < 0) {
        xERROR("消息发送失败");
    } else {
        xINFO("网关 {} 注册成功", id);
    }
}


void
typhon::tcp::Proc::on_handle(Session::Ptr s, core::PKx<core::Host> pkx) noexcept {
    if (!s->authed()) {
        xWARN("{} 网关未鉴权", s->remote_addr());
        return;
    }

    auto h = server_->get_handler(pkx.pk()->id);
    if (!h) {
        xWARN("no handler for pk_id {}, from {}", pkx.pk()->id, s->remote_addr());
        // TODO: 通知网关, 业务服务不存在, 需要主动断开与客户端的连接
    }
    h(s, pkx);
}