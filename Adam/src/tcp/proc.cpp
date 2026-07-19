#include "tcp/config.hpp"
#include "tcp/server.hpp"
#include "tcp/proc.hpp"
#include "core/error.hpp"


void
adam::tcp::Proc::run() noexcept {
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
adam::tcp::Proc::init() noexcept {
    epfd_ = ::epoll_create1(0);
    ASSERT(epfd_ != INVALID_SOCKET, "epoll_create1 failed: errno = {}, errstr = {}", errno, ::strerror(errno));

    evfd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT(evfd_ != INVALID_SOCKET, "eventfd failed: errno = {}, errstr = {}", errno, ::strerror(errno));

    ::epoll_event ev;
    ev.data.fd = evfd_;
    ev.events = EPOLLIN | EPOLLET;
    ASSERT(::epoll_ctl(epfd_, EPOLL_CTL_ADD, evfd_, &ev) == 0, "errno = {}, errstr = {}", errno, ::strerror(errno));
}


void
adam::tcp::Proc::release() noexcept {
    if (epfd_ != INVALID_SOCKET) {
        ::close(epfd_);
        epfd_ = INVALID_SOCKET;
    }

    if (evfd_ != INVALID_SOCKET) {
        ::close(evfd_);
        evfd_ = INVALID_SOCKET;
    }

    evque_.clear([](core::QEvent* qe) {
        if (qe->type == core::QEvent::Type::TcpRecv) {
            auto* rbuf = (core::TcpRecvArg*)qe->arg.ptr;
            ::mi_free(rbuf);
        }
        delete qe;
    });
}


void
adam::tcp::Proc::on_event_handle(const ::epoll_event& ev) noexcept {
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

    drain_evque();
    evq_wkring_.store(false);
    drain_evque();
}


void
adam::tcp::Proc::on_recv_handle(core::QEvent* qe) noexcept {
    auto* rbuf = (core::TcpRecvArg*)qe->arg.ptr;
    auto sess = server_->get_session(rbuf->fd);
    if (sess == nullptr) {
        ::mi_free(rbuf);
        return;
    }

    if (sess->input(rbuf->data, rbuf->len) != xOK) {
        ::mi_free(rbuf);
        return;
    }

    alignas(core::Package) static thread_local uint8_t buf[sizeof(core::Package) + (core::PKG_MAX_LEN - core::PKG_HDR_LEN)];
    core::Package* pk = (core::Package*)buf;

    while (sess->recv(pk) == xOK) {
        switch (pk->data.id) {
        case PKID_PING:
            on_ping(sess, pk);
            break;

        case PKID_REGIST_REQ:
            on_regist(sess, pk);
            break;

        default:
            on_handle(sess, pk);
            break;
        }
    }

    ::mi_free(rbuf);
}


void
adam::tcp::Proc::on_send_handle(core::QEvent* qe) noexcept {
    auto fd = (SOCKET)(uintptr_t)qe->arg.ptr;
    auto sess = server_->get_session(fd);
    if (sess != nullptr) {
        int n = sess->send();
        if (n < 0) {
            xWARN("failed to send data to fd {}, err = {}", fd, -n);
        }
    }
}


void
adam::tcp::Proc::on_add_sess_handle(core::QEvent* qe) noexcept {
    auto fd = (SOCKET)(uintptr_t)qe->arg.ptr;
    server_->add_session(fd, this);
}


void
adam::tcp::Proc::on_rmv_sess_handle(core::QEvent* qe) noexcept {
    auto fd = (SOCKET)(uintptr_t)qe->arg.ptr;
    server_->remove_session(fd, false);
}


void
adam::tcp::Proc::check_timeout() noexcept {
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
adam::tcp::Proc::on_ping(Session::Ptr s, core::Package* pk) noexcept {
    pk->data.id = PKID_PONG;
    if (s->send(*pk) < 0) {
        xERROR("发送消息失败");
    }
}


void
adam::tcp::Proc::on_regist(Session::Ptr s, core::Package* pk) noexcept {
    // connector 侧 regist 用小端写的 id, 这里按小端读; 结果码同样小端
    uint32_t id = core::u32_to_le(*(uint32_t*)pk->data.payload);

    pk->data.id = PKID_REGIST_RSP;
    *(uint32_t*)pk->data.payload = core::u32_to_le(0);   // 0 = 注册成功

    s->set_id(id);

    if (s->send(*pk) < 0) {
        xERROR("消息发送失败");
    } else {
        xINFO("网关 {} 注册成功", id);
    }
}


void
adam::tcp::Proc::on_handle(Session::Ptr s, core::Package* pk) noexcept {
    if (!s->authed()) {
        xWARN("{} 网关未鉴权", s->remote_addr());
        return;
    }

    auto h = server_->get_handler((uint16_t)pk->data.id);
    if (!h) {
        xWARN("no handler for pk_id {}, from {}", pk->data.id, s->remote_addr());
        // TODO: 通知网关, 业务服务不存在, 需要主动断开与客户端的连接
        return;
    }
    h(s, pk);
}


void
adam::tcp::Proc::drain_evque() noexcept {
    size_t i, n;
    core::QEvent* qes[16];
    while ((n = evque_.try_dequeue_bulk(qes, 16)) > 0) {
        for (i = 0; i < n; ++i) {
            switch (qes[i]->type) {
            case core::QEvent::Type::Stop:
                // nothing to do
                break;

            case core::QEvent::Type::TcpRecv:
                on_recv_handle(qes[i]);
                break;

            case core::QEvent::Type::TcpSend:
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