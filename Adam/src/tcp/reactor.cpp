#include "core/error.hpp"
#include "core/proto/pkid_terminal_enter.hpp"
#include "tcp/config.hpp"
#include "tcp/reactor.hpp"
#include "tcp/server.hpp"
#include "core/proto/pkid_regist_terminal.hpp"


// ET 下单次 recv 的读缓冲(循环读到 EAGAIN 为止), 每 reactor 线程一份
static constexpr int RBUF_SIZE  = 16 * 1024;
// 一次 epoll_wait 最多取的事件数(reactor 名下可能有很多连接 fd)
static constexpr int MAX_EVENTS = 256;


void
adam::tcp::Reactor::run() noexcept {
    auto expected = core::State::Stopped;
    if (!state_.compare_exchange_strong(expected, core::State::Starting)) {
        return;
    }

    init();

    int i, n;
    ::epoll_event events[MAX_EVENTS];

    state_.store(core::State::Running);

    while (running()) {
        n = ::epoll_wait(epfd_, events, MAX_EVENTS, 10);
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
            if (ev.data.fd == mfd_) {
                on_event_handle(ev);
            } else {
                on_session_handle(ev);
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
adam::tcp::Reactor::init() noexcept {
    epfd_ = ::epoll_create1(0);
    ASSERT(epfd_ != INVALID_SOCKET, "epoll_create1 failed: errno = {}, errstr = {}", errno, ::strerror(errno));

    mfd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT(mfd_ != INVALID_SOCKET, "eventfd failed: errno = {}, errstr = {}", errno, ::strerror(errno));

    ::epoll_event ev;
    ev.data.fd = mfd_;
    ev.events = EPOLLIN | EPOLLET;
    ASSERT(::epoll_ctl(epfd_, EPOLL_CTL_ADD, mfd_, &ev) == 0, "errno = {}, errstr = {}", errno, ::strerror(errno));
}


void
adam::tcp::Reactor::release() noexcept {
    if (epfd_ != INVALID_SOCKET) {
        ::close(epfd_);
        epfd_ = INVALID_SOCKET;
    }

    if (mfd_ != INVALID_SOCKET) {
        ::close(mfd_);
        mfd_ = INVALID_SOCKET;
    }

    // 残留消息只剩 Stop / SessionConnected(arg 是 fd, 无堆内存), 直接 delete
    mque_.clear([](Message* m) {
        delete m;
    });
}


void
adam::tcp::Reactor::remove_session(SOCKET fd) noexcept {
    auto itr = sesss_.find(fd);
    if (itr == sesss_.end()) {
        return;
    }

    auto s = itr->second;
    ::epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, nullptr);
    sesss_.erase(itr);
    server_->hook()->on_disconnected(s);
}


void
adam::tcp::Reactor::on_event_handle(const ::epoll_event& ev) noexcept {
    if (!(ev.events & EPOLLIN)) {
        return;
    }

    int err = 0;
    while (1) {
        uint64_t event;
        auto n = ::read(mfd_, &event, sizeof(event));
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
    mq_workering_.store(false);
    drain_evque();
}


void
adam::tcp::Reactor::on_session_connected(Message* m) noexcept {
    SOCKET fd = m->arg.fd;
    auto s = Session::create(fd, this);

    ::epoll_event ev;
    ev.events  = EPOLLIN | EPOLLET | EPOLLOUT;
    ev.data.fd = fd;
    if (::epoll_ctl(epfd_, EPOLL_CTL_ADD, fd, &ev) != 0) {
        xERROR("epoll_ctl ADD failed: fd = {}, errno = {}, errstr = {}", fd, errno, ::strerror(errno));
        return;
    }

    if (server_->hook()->on_connected(s) != 0) {
        ::epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, nullptr);
        return;
    }

    sesss_.emplace(fd, s);
    if (session_handle(s) != 0) {
        remove_session(fd);
    }
}


void
adam::tcp::Reactor::on_session_handle(const ::epoll_event& ev) noexcept {
    SOCKET fd = ev.data.fd;
    auto s = get_session(fd);
    if (s == nullptr) {
        return;
    }

    if (ev.events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
        remove_session(fd);
        return;
    }

    bool del = false;

    if (ev.events & EPOLLIN) {
        del = session_handle(s) != 0;
    }

    if (!del && (ev.events & EPOLLOUT) && s->send() < 0) {
         del = true;
    }

    if (del) {
        remove_session(fd);
    }
}


int
adam::tcp::Reactor::session_handle(Session::Ptr s) noexcept {
    static thread_local uint8_t buf[RBUF_SIZE];
    alignas(core::Package) static thread_local uint8_t pkbuf[sizeof(core::Package) + (core::PKG_MAX_LEN - core::PKG_HDR_LEN)];
    core::Package* pk = (core::Package*)pkbuf;

    while (1) {
        ssize_t n = ::recv(s->fd(), buf, RBUF_SIZE, 0);
        if (n > 0) {
            if (s->input(buf, (size_t)n) != xOK) {
                return -1;
            }

            int res;
            while ((res = s->recv(pk)) == xOK) {
                switch (pk->data.id) {
                case PKID_PING:
                    res = on_ping(s, pk);
                    break;

                case PKID_REG_BKD_REQ:
                    res = on_regist(s, pk);
                    break;

                case PKID_TER_ENT_REQ:
                    res = on_terminal_enter(s, pk);
                    break;

                case PKID_TER_LEA_REQ:
                    res = on_terminal_leave(s, pk);
                    break;

                default:
                    res = on_package_handle(s, pk);
                    break;
                }

                if (res != xOK && res != xAGAIN) {
                    break;
                }
            }

            if (res != xAGAIN) {
                return -1;
            }
        } else if (n == 0) {
            return -1;
        } else {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return 0;
            }
            xERROR("recv failed: fd = {}, errno = {}, errstr = {}", s->fd(), errno, ::strerror(errno));
            return -1;
        }
    }
}


void
adam::tcp::Reactor::check_timeout() noexcept {
    const auto to = Conf::instance()->server()->timeout;

    // remove_session 会 erase, 不能边遍历边删 —— 先收集超时 fd, 再统一摘除。
    // thread_local vector 复用, 稳态无 per-call 分配。
    static thread_local std::vector<SOCKET> expired;
    expired.clear();

    for (auto& [fd, s] : sesss_) {
        if (s->last_recv_ms() + to < tnow_) {
            expired.push_back(fd);
        }
    }

    for (SOCKET fd : expired) {
        remove_session(fd);
    }
}


int
adam::tcp::Reactor::on_ping(Session::Ptr s, core::Package* pk) noexcept {
    pk->data.id = PKID_PONG_BKD;
    if (s->send(*pk) < 0) {
        xERROR("发送消息失败");
    }

    return 0;
}


int
adam::tcp::Reactor::on_regist(Session::Ptr s, core::Package* pk) noexcept {
    // connector 侧 regist 用小端写的 id, 这里按小端读; 结果码同样小端
    uint32_t id = core::u32_to_le(*(uint32_t*)pk->data.payload);

    pk->data.id = PKID_REG_BKD_RSP;
    pk->meta.len = core::PKG_HDR_LEN + sizeof(uint32_t);
    *(uint32_t*)pk->data.payload = core::u32_to_le(0);

    s->set_id(id);

    if (s->send(*pk) < 0) {
        xERROR("消息发送失败");
    } else {
        xINFO("网关 {} 注册成功", id);
    }

    return 0;
}


int
adam::tcp::Reactor::on_terminal_enter(Session::Ptr s, core::Package *pk) noexcept {
    ASSERT(s->reactor() == this, "terminal enter 不在属主 reactor 线程");

    if (!s->authed()) {
        return xERR;
    }

    core::TerminalEnterReq req;
    if (req.decode(pk->data.payload, pk->payload_length()) != xOK) {
        return xERR;
    }

    uint32_t code = 0;

    if (req.conv != pk->meta.conv) {
        xWARN("info.conv: {} != pk->meta.conv: {}", req.conv, pk->meta.conv);
        code = 1;
    } else if (req.ip != pk->meta.src_addr) {
        xWARN("info.ip: {} != pk->meta.src_addr: {}", req.ip, pk->meta.src_addr);
        code = 1;
    }

    if (code == 0) {
        uint32_t prev = server_->directory()->exchange(req.uid, index_);
        if (prev != Directory::NPOS && prev != index_) {
            server_->reactor(prev)->notify(new Message(Message::Type::TerminalKick, req.uid));
        } else {
            auto itr = terminal_router_.find(req.uid);
            if (itr != terminal_router_.end() && itr->second->sess() != s) {
                kick_terminal(req.uid);
            }
        }

        terminal_router_[req.uid] = Terminal::create(req.uid, req.conv, req.ip, req.port, s);
    }

    core::TerminalEnterRsp rsp;
    rsp.uid  = req.uid;
    rsp.code = code;

    pk->data.id = PKID_TER_ENT_RSP;
    uint32_t src_id = pk->data.src_id;
    pk->data.src_id = pk->data.dst_id;
    pk->data.dst_id = src_id;
    pk->meta.len = core::PKG_HDR_LEN + core::TerminalEnterRsp::LEN;
    rsp.encode(pk->data.payload);

    if (s->send(*pk) < 0) {
        xERROR("TER_ENT_RSP 发送失败: uid = {}", req.uid);
    }

    return xOK;
}


int
adam::tcp::Reactor::on_terminal_leave(Session::Ptr s, core::Package *pk) noexcept {
    return 0;
}


void
adam::tcp::Reactor::kick_terminal(uint32_t uid) noexcept {
    auto itr = terminal_router_.find(uid);
    if (itr == terminal_router_.end()) {
        return;
    }

    auto t = itr->second;
    alignas(core::Package) uint8_t kbuf[sizeof(core::Package) + 8];
    auto* kpk = (core::Package*)kbuf;

    kpk->meta.len      = core::PKG_HDR_LEN + 8;
    kpk->meta.conv     = t->conv();
    kpk->meta.src_addr = 0;
    kpk->data.id       = PKID_KIC_TER_REQ;
    kpk->data.src_id   = Conf::instance()->server()->id;
    kpk->data.dst_id   = t->sess()->id();
    kpk->data.seq      = 0;
    *(uint32_t*)(kpk->data.payload)     = core::u32_to_le(t->uid());
    *(uint32_t*)(kpk->data.payload + 4) = core::u32_to_le(t->conv());

    if (t->sess()->send(*kpk) < 0) {
        xERROR("KIC_TER_REQ 发送失败: uid = {}", t->uid());
    }

    terminal_router_.erase(itr);
}


int
adam::tcp::Reactor::on_package_handle(Session::Ptr s, core::Package* pk) noexcept {
    if (!s->authed()) {
        xWARN("{} 网关未鉴权", s->remote_addr());
        return -1;
    }

    auto h = server_->get_handler((uint16_t)pk->data.id);
    if (!h) {
        xWARN("no handler for pk_id {}, from {}", pk->data.id, s->remote_addr());
        // TODO: 通知网关, 业务服务不存在, 需要主动断开与客户端的连接
        return 0;
    }
    h(s, pk);

    return 0;
}


void
adam::tcp::Reactor::drain_evque() noexcept {
    size_t i, n;
    Message* ms[16];
    while ((n = mque_.try_dequeue_bulk(ms, 16)) > 0) {
        for (i = 0; i < n; ++i) {
            switch (ms[i]->type) {
            case Message::Type::Stop:
                // 仅用于唤醒, state_ 已在 stop() 里置 Stopping, 本轮返回后循环自然退出
                break;

            case Message::Type::SessionConnected:
                on_session_connected(ms[i]);
                break;

            case Message::Type::TerminalKick:
                kick_terminal(ms[i]->arg.id);
                break;

            default:
                break;
            }

            delete ms[i];
        }
    }
}
