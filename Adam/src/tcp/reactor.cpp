#include "core/error.hpp"
#include "core/proto/pid_terminal_kick.hpp"
#include "core/proto/pid_terminal_regist.hpp"
#include "core/proto/pid_terminal_enter.hpp"
#include "core/proto/pid_terminal_offline.hpp"
#include "tcp/config.hpp"
#include "tcp/reactor.hpp"
#include "tcp/server.hpp"


// ET 下单次 recv 的读缓冲(循环读到 EAGAIN 为止), 每 reactor 线程一份
static constexpr int RBUF_SIZE  = 16 * 1024;
// 一次 epoll_wait 最多取的事件数(reactor 名下可能有很多连接 fd)
static constexpr int MAX_EVENTS = 256;


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
}


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

    mque_.clear([](Message* m) {
        if (m->type == Message::Type::SessionConnected) {
            ::close(m->arg.fd);
        }
        delete m; 
    });

    release();
    state_.store(core::State::Stopped);
}


// 按 uid 找到终端并踢除; 组包发送由 Terminal 自己完成.
// 只发通知, 不在此删档 -- 删除统一由 OFF / 断连清扫负责:
//   业务踢人: 等网关摘掉会话后扇出 OFF 再删(踢失败也不会"提前忘记");
//   顶号:     add_terminal 末尾的覆盖写自然替换旧档(同 reactor),
//             跨 reactor 时旧档留在旧 reactor, 等它那条连接上的 OFF.
void
adam::tcp::Reactor::kick_terminal(uint32_t uid, uint32_t code) noexcept {
    auto t = get_terminal(uid);
    if (t != nullptr) {
        t->kick(code, this);
    }
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
adam::tcp::Reactor::remove_session(SOCKET fd) noexcept {
    auto itr = sesss_.find(fd);
    if (itr == sesss_.end()) {
        return;
    }

    auto s = itr->second;
    ::epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, nullptr);
    sesss_.erase(itr);

    // 断连清扫: 该连接名下所有终端删档(网关崩溃/掉线的粗粒度兜底)
    for (auto itr = ters_.begin(); itr != ters_.end(); ) {
        auto t = itr->second;
        ++itr;
        if (t->sess() == s) { 
            remove_terminal(t->uid(), PERR_TER_GW_LOST);
        }
    }

    if (s->id() > 0) {
        server_->hook()->on_serv_unregisted(s);
    }

    server_->hook()->on_sess_disconnected(s);
}


int
adam::tcp::Reactor::add_terminal(Terminal::Ptr t) noexcept {
    ASSERT(t->sess()->reactor() == this, "add_terminal 不在属主 reactor 线程");

    if (server_->hook()->on_terminal_enter(t) != 0) {
        xWARN("终端 {} 被业务钩子拒绝进入", t->uid());
        t->kick(PERR_TER_REJECTED, this);
        return xERR_REJECTED;
    }

    uint32_t prev = server_->directory()->exchange(t->uid(), index_);
    if (prev != Directory::NPOS && prev != index_) {
        server_->reactor(prev)->notify(new Message(Message::Type::TerminalKick, t->uid(), (uint32_t)PERR_TER_TAKEOVER));
    } else {
        // 本 reactor: 旧档挂在别的连接 = 顶号; 同连接 = 重报, 静默覆盖
        auto old = get_terminal(t->uid());
        if (old != nullptr && old->conv() != t->conv()) {
            kick_terminal(t->uid(), PERR_TER_TAKEOVER);
        }
    }

    ters_[t->uid()] = std::move(t);
    return 0;
}


void
adam::tcp::Reactor::remove_terminal(uint32_t uid, uint32_t code) noexcept {
    auto t = get_terminal(uid);
    if (t != nullptr) {
        server_->hook()->on_terminal_leave(t, code);
    }

    ters_.erase(uid);
    server_->directory()->erase_if(uid, index_);
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
            int rc = s->input(buf, (size_t)n);
            if (rc != xOK) {
                xERROR("{} 入缓冲失败, 断开: {}({})", s->remote_addr(), rc, core::str_error(rc));
                return rc;
            }

            int res;
            while ((res = s->recv(pk)) == xOK) {
                switch (pk->data.pid) {
                case PID_PING:
                    res = on_ping(s, pk);
                    break;

                case PID_BKD_REG_REQ:
                    res = on_serv_regist(s, pk);
                    break;

                case PID_TER_ENT_REQ:
                    res = on_terminal_enter_req(s, pk);
                    break;

                case PID_TER_OFF_NTF:
                    res = on_terminal_offline_notify(s, pk);
                    break;

                case PID_TER_LEA_REQ:
                    res = on_terminal_leave_req(s, pk);
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
                // 半包(xAGAIN)之外的任何结果都断连: 要么帧非法, 要么某个 handler 报错
                xERROR("{} 处理消息失败, 断开: {}({})", s->remote_addr(), res, core::str_error(res));
                return res;
            }
        } else if (n == 0) {
            return xERR_TCP_CLOSED;
        } else {
            if (errno == EINTR) {
                continue;
            }

            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return xOK;
            }

            xERROR("recv failed: fd = {}, errno = {}, errstr = {}", s->fd(), errno, ::strerror(errno));
            return -errno;
        }
    }
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

    if (server_->hook()->on_sess_connected(s) != 0) {
        ::epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, nullptr);
        return;
    }

    add_session(s);
}


void
adam::tcp::Reactor::check_timeout() noexcept {
    const auto to = Conf::instance()->server()->timeout;

    // remove_session 会 erase, 不能边遍历边删 -- 先收集超时 fd, 再统一摘除
    // thread_local vector 复用, 稳态无 per-call 分配
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
    pk->data.pid = PID_PONG;
    if (s->send(*pk) < 0) {
        xERROR("发送消息失败");
    }

    return 0;
}


int
adam::tcp::Reactor::on_serv_regist(Session::Ptr s, core::Package* pk) noexcept {
    // connector 侧 regist 用小端写的 id, 这里按小端读; 结果码同样小端
    uint32_t id = core::u32_to_le(*(uint32_t*)pk->data.payload);

    pk->data.pid = PID_BKD_REG_RSP;
    pk->meta.len = core::PKG_HDR_LEN + sizeof(uint32_t);
    *(uint32_t*)pk->data.payload = core::u32_to_le(0);

    s->set_id(id);

    if (s->send(*pk) < 0) {
        xERROR("消息发送失败");
        return 0;
    }

    server_->hook()->on_serv_registed(s);
    return 0;
}


int
adam::tcp::Reactor::on_terminal_enter_req(Session::Ptr s, core::Package *pk) noexcept {
    ASSERT(s->reactor() == this, "terminal enter 不在属主 reactor 线程");

    if (!s->authed()) {
        return xERR;
    }

    core::TerminalEnterReq req;
    if (req.decode(pk->data.payload, pk->payload_length()) != xOK) {
        return xERR;
    }

    uint32_t code = SERR_OK;

    // payload 里的 conv/ip 是客户端自报的, meta 里的是网关按自身会话状态填的
    // (Session::recv 里赋值, 客户端碰不到). 两者对撞, 确认这条登记确实来自
    // 网关经手的那个会话, 而不是伪造的 uid.
    if (req.conv != pk->meta.conv) {
        xWARN("info.conv: {} != pk->meta.conv: {}", req.conv, pk->meta.conv);
        code = SERR_MISMATCH;
    } else if (req.ip != pk->meta.src_addr) {
        xWARN("info.ip: {} != pk->meta.src_addr: {}", req.ip, pk->meta.src_addr);
        code = SERR_MISMATCH;
    }

    if (code == SERR_OK) {
        auto t = Terminal::create(req.uid, req.conv, req.ip, req.port, s);
        if (add_terminal(t) == 0) {
            if (s->id() != pk->data.src_id) {
                t->bind();
            }
        } else {
            code = SERR_CONFLICT;
        }
    }

    core::TerminalEnterRsp rsp;
    rsp.uid  = req.uid;
    rsp.code = code;

    pk->data.pid = PID_TER_ENT_RSP;
    uint32_t src_id = pk->data.src_id;
    pk->data.src_id = pk->data.dst_id;
    pk->data.dst_id = src_id;
    pk->meta.len = core::PKG_HDR_LEN + core::TerminalEnterRsp::LEN;
    rsp.encode(pk->data.payload, core::TerminalEnterRsp::LEN);

    if (s->send(*pk) < 0) {
        xERROR("TER_ENT_RSP 发送失败: uid = {}", req.uid);
    }

    return xOK;
}


int
adam::tcp::Reactor::on_terminal_offline_notify(Session::Ptr s, core::Package* pk) noexcept {
    ASSERT(s->reactor() == this, "terminal offline notify 不在属主 reactor 线程");

    if (!s->authed()) {
        return xERR;
    }

    core::TerminalOfflineNotify ntf;
    if (ntf.decode(pk->data.payload, pk->payload_length()) != xOK) {
        return xERR;
    }

    auto t = get_terminal(ntf.uid);
    if (t == nullptr || t->sess() != s || t->conv() != pk->meta.conv) {
        return xOK;
    }

    remove_terminal(ntf.uid, ntf.code);
    return xOK;
}


int
adam::tcp::Reactor::on_terminal_leave_req(Session::Ptr s, core::Package* pk) noexcept {
    ASSERT(s->reactor() == this, "terminal leave req 不在属主 reactor 线程");

    if (!s->authed()) {
        return xERR;
    }

    uint32_t uid = pk->data.src_id;

    auto t = get_terminal(uid);
    if (t != nullptr && t->sess() == s && t->conv() == pk->meta.conv) {
        t->unbind();
        remove_terminal(uid, PERR_TER_LEAVE);
    }

    pk->data.pid = PID_TER_LEA_RSP;
    uint32_t src_id = pk->data.src_id;
    pk->data.src_id = pk->data.dst_id;
    pk->data.dst_id = src_id;
    pk->meta.len = core::PKG_HDR_LEN;

    if (s->send(*pk) < 0) {
        xERROR("TER_LEA_RSP 发送失败: uid = {}", uid);
    }

    return xOK;
}


int
adam::tcp::Reactor::on_package_handle(Session::Ptr s, core::Package* pk) noexcept {
    if (!s->authed()) {
        xWARN("{} 网关未鉴权", s->remote_addr());
        return xERR_NOT_AUTH;
    }

    auto h = server_->get_handler((uint16_t)pk->data.pid);
    if (!h) {
        xWARN("no handler for pk_id {}, from {}", pk->data.pid, s->remote_addr());
        // TODO: 通知网关, 业务服务不存在, 需要主动断开与客户端的连接
        return 0;
    }

    auto t = get_terminal(pk->data.src_id);
    if (t == nullptr) {
        return 0;
    }

    Server::Context ctx(this, t.get());
    h(ctx, pk);
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
                kick_terminal(ms[i]->arg.kick.uid, ms[i]->arg.kick.code);
                break;

            default:
                break;
            }

            delete ms[i];
        }
    }
}
