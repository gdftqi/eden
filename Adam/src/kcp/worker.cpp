#include "kcp/worker.hpp"
#include "kcp/server.hpp"
#include "tcp/connector.hpp"
#include "core/proto/pid_terminal_regist.hpp"
#include "core/proto/pid_terminal_enter.hpp"
#include "core/proto/pid_terminal_leave.hpp"
#include "core/proto/pid_terminal_kick.hpp"
#include "core/proto/pid_terminal_bind.hpp"
#include "core/proto/pid_terminal_unbind.hpp"


static constexpr int MAX_EVENTS    = 64;
static constexpr int INTERVAL_MS   = 10;
static constexpr int TCP_RBUF_SIZE = 8 * 1024;


adam::kcp::Worker::Worker(Server* s, int idx) noexcept
    : server_(s)
    , index_(idx) {
    ASSERT(server_ != nullptr && idx >= 0, "invalid server or idx");
    event_ = server_->hook();

    for (int i = 0; i < MAX_RECV; ++i) {
        auto hdr = &rmsgs_[i].msg_hdr;
        hdr->msg_iov = &riovecs_[i];
        hdr->msg_iovlen = 1;
        hdr->msg_name = &raddrs_[i];
        hdr->msg_namelen = sizeof(raddrs_[i]);

        riovecs_[i].iov_len = core::UDP_MTU;
        riovecs_[i].iov_base = ::mi_malloc(core::UDP_MTU);
        ASSERT(riovecs_[i].iov_base != nullptr, "failed to allocate memory for riovec"); 
    }

    ufd_ = core::udp_bind(server_->host(), Conf::instance()->sndbuf(), Conf::instance()->rcvbuf());
    ASSERT(ufd_ != INVALID_SOCKET, "创建 udp fd 失败: errno = {}, errstr = {}", errno, ::strerror(errno));
    ::ikcp_allocator(::mi_malloc, ::mi_free);
}


adam::kcp::Worker::~Worker() noexcept {
    for (int i = 0; i < MAX_RECV; ++i) {
        ::mi_free(riovecs_[i].iov_base);
    }
}


void
adam::kcp::Worker::run() noexcept {
    auto expected = core::State::Stopped;
    if (!state_.compare_exchange_strong(expected, core::State::Starting)) {
        return;
    }

    init();

    int i, n;
    ::epoll_event events[MAX_EVENTS];
    const auto* const evptr = &evfd_;
    const auto* const ufdptr = &ufd_;

    state_.store(core::State::Running);

    while (running()) {
        n = ::epoll_wait(epfd_, events, MAX_EVENTS, INTERVAL_MS);
        if (n == -1) {
            if (errno == EINTR) {
                continue;
            }
            xERROR("epoll_wait failed: errno = {}, errstr = {}", errno, ::strerror(errno));
            break;
        }

        tnow_ = utils::systime_ms();
        for (i = 0; i < n; ++i) {
            auto& ev = events[i];
            if (ev.data.ptr == evptr) {
                on_event_handle(ev);
            } else if (ev.data.ptr == ufdptr) {
                on_udp_handle(ev);
            } else {
                on_serv_handle(ev);
            }
        }
        update();
    }

    sesss_.clear();
    servs_.clear();

    for (auto* dg: dg_que_) {
        dg_pool_.release(dg);
    }
    dg_que_.clear();

    mque_.clear([](Message* qe) {
        if (qe->type == Message::Type::EnsureBackend) {
            ::mi_free(qe->arg.ptr);
        }
        delete qe; 
    });

    release();
    state_.store(core::State::Stopped);
}


int
adam::kcp::Worker::output(const char *buf, int len, IKCPCB* kcpcb) noexcept {
    auto* s   = (Session*)kcpcb->user;
    auto* svr = s->worker();
    auto* dg  = svr->dg_pool_.acquire(s->addr(), s->addrlen(), buf, len, svr->tnow());
    svr->dg_que_.emplace_back(dg);
    return 0;
}


void
adam::kcp::Worker::init() noexcept {
    epfd_ = ::epoll_create1(0);
    ASSERT(epfd_ != INVALID_SOCKET, "创建 epoll fd 失败: errno = {}, errstr = {}", errno, ::strerror(errno));

    evfd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT(evfd_ != INVALID_SOCKET, "创建 停止事件 fd 失败: errno = {}, errstr = {}", errno, ::strerror(errno));

    ::epoll_event ev;
    ev.data.ptr = &evfd_;
    ev.events = EPOLLIN | EPOLLET;
    ASSERT(::epoll_ctl(epfd_, EPOLL_CTL_ADD, evfd_, &ev) == 0, "errno = {}, errstr = {}", errno, ::strerror(errno));

    ev.data.ptr = &ufd_;
    // 取消 ET 模式, 目的是加强防御.
    // 因为攻击者有可能大量发包, 使用 recvmmsg 不停的读取数据从而导致DOS.
    // 所以作法是只能取有限数量的包, 如果还有未读数据则会被再次唤醒.
    ev.events = EPOLLIN;
    ASSERT(::epoll_ctl(epfd_, EPOLL_CTL_ADD, ufd_, &ev) == 0, "errno = {}, errstr = {}", errno, ::strerror(errno));
}


void
adam::kcp::Worker::release() noexcept {
    if (epfd_ != INVALID_SOCKET) {
        ::close(epfd_);
        epfd_ = INVALID_SOCKET;
    }

    if (evfd_ != INVALID_SOCKET) {
        ::close(evfd_);
        evfd_ = INVALID_SOCKET;
    }

    if (ufd_ != INVALID_SOCKET) {
        ::close(ufd_);
        ufd_ = INVALID_SOCKET;
    }
}


int
adam::kcp::Worker::add_session(uint32_t conv, Session::Ptr s) noexcept {
    auto [_, res] = sesss_.emplace(conv, s);
    if (res && event_->on_sess_connected(s)) {
        sesss_.erase(conv);
        return -1;
    }
    return 0;
}


void
adam::kcp::Worker::remove_session(uint32_t conv) noexcept {
    auto itr = sesss_.find(conv);
    if (itr != sesss_.end()) {
        auto sess = itr->second;
        sesss_.erase(itr);
        terminal_off(sess);   // 先通知绑定集里的后端清档, 再回调业务
        event_->on_sess_disconnected(sess);
    }
}


void
adam::kcp::Worker::remove_serv(tcp::Connector::Ptr c) noexcept {
    ASSERT(::epoll_ctl(epfd_, EPOLL_CTL_DEL, c->fd(), nullptr) == 0, "epoll_ctl failed: errno = {}, errstr = {}", errno, ::strerror(errno));
    server_->hook()->on_serv_disconnected(c);
    servs_.erase(c->id());
}


void
adam::kcp::Worker::on_event_handle(const ::epoll_event& ev) noexcept {
    // 目前事件队列只有 发现后端服务事件 , 所以不需要判断 ev.data.ptr

    if (ev.events & EPOLLIN) {
        uint64_t event;
        while (1) {
            auto n = ::read(evfd_, &event, sizeof(event));
            if (n == -1) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    xERROR("read eventfd failed: errno = {}, errstr = {}", errno, ::strerror(errno));
                }
                break;
            }
        }
    } // if (ev.events & EPOLLIN);

    drain_qevent();
    mq_workring_.store(false);
    drain_qevent();
}


void
adam::kcp::Worker::on_udp_handle(const ::epoll_event& ev) noexcept {
    // ufd_ 是 EPOLLET, 且和后端 Connector fd / 控制 eventfd 共用同一个 epoll 线程.
    // MAX_ROUND 只是防洪峰的安全阀(没吃完的包留在内核缓冲区, ET 下一有新包到达即自愈,不会丢),
    // 不是常规吞吐限流。正常流量下应远打不到这个 cap, 真正起作用的场景是攻击/洪峰.
    //
    // 取值公式:
    //   MAX_ROUND = ceil((总QPS / worker数) * INTERVAL_MS(10ms) * 余量系数(5~10) / MAX_RECV(128))
    constexpr int MAX_ROUND = 16;

    alignas(core::Package) static thread_local uint8_t buf[sizeof(core::Package) + (core::PKG_MAX_LEN - core::PKG_DATA_LEN)];
    core::Package* pk = (core::Package*)buf;

    int res = 0;
    if (ev.events & EPOLLERR) {
        socklen_t len = sizeof(res);
        ASSERT(::getsockopt(ufd_, SOL_SOCKET, SO_ERROR, &res, &len) == 0, "errno = {}, errstr = {}", errno, ::strerror(errno));
        if (res != 0) {
            xERROR("socket error: errno = {}, errstr = {}", res, ::strerror(res));
            return;
        }
    }
    
    if (ev.events & EPOLLIN) {
        int i, n, round = 0;
        while (round++ < MAX_ROUND) {
            for (i = 0; i < MAX_RECV; ++i) {
                riovecs_[i].iov_len = core::UDP_MTU;
            }

            n = ::recvmmsg(ufd_, rmsgs_, MAX_RECV, MSG_DONTWAIT, nullptr);
            if (n <= 0) {
                if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                    xERROR("recvmsg error: errno = {}, errstr = {}", errno, ::strerror(errno));
                }
                break;
            }

            for (i = 0; i < n; ++i) {
                if (rmsgs_[i].msg_hdr.msg_flags & MSG_TRUNC) {
                    // 判断 UDP 包被截断了
                    // 内核收到的包长度超过了 UDP_MTU, 后面的数据被丢弃
                    // 如果出现这种情况, 一定是攻击行为
                    // TODO: 记录攻击行为
                    xWARN("UDP truncated from {} dropped", core::sockaddr_to_string((sockaddr*)rmsgs_[i].msg_hdr.msg_name));
                    continue;
                }

                auto& msg = rmsgs_[i];
                if (msg.msg_len < core::ENVELOPE_MAC_LEN + core::KCP_HDR_LEN) {
                    // 如果长KCP协议头 + SIPHASH 长度, 说明这个包不合法
                    // TODO: 记录攻击行为
                    continue;
                }

                auto  hdr    = &rmsgs_[i].msg_hdr;
                auto* raw    = (uint8_t*)hdr->msg_iov[0].iov_base;
                auto  msglen = (long)msg.msg_len;

                auto conv = Session::getconv(raw, msglen);
                if (conv == 0) {
                    // TODO: 记录攻击行为
                    continue;
                }

                auto s = get_session(conv);
                bool is_new = false;
                if (s == nullptr) {
                    s = Session::create(conv, this, hdr->msg_name, hdr->msg_namelen);
                    is_new = true;
                }

                res = s->input(raw, msglen, hdr->msg_name, hdr->msg_namelen);
                if (res != xOK) {
                    continue;
                }

                if (is_new && add_session(conv, s) != 0) {
                    continue;
                }
                
                while (true) {
                    res = s->recv(pk);
                    if (res == xAGAIN) {
                        // 没有更多消息了
                        break;
                    } else if (res == xDUP) {
                        // 幂等重复, 跳过
                        continue;
                    } else if (res < 0) {
                        // 协议错误
                        // TODO: 记录攻击行为, 并且封禁对端 IP 一段时间
                        remove_session(s->conv());
                        break;
                    }

                    // 会话级不变量: 鉴权后每个包都必须带本会话的 uid。
                    // 鉴权前 uid 尚未确立(authed() == uid_ > 0), 故 REGIST 天然豁免, 无需按 PID 特判。
                    // 少了这道闸, 客户端可伪造 src_id 冒名他人, 或伪装成"网关发起"绕过后端的来源判别。
                    if (s->authed() && pk->data.src_id != s->uid()) {
                        xERROR("{} src_id 伪造: [{}]", s->to_json(), pk->data.src_id);
                        remove_session(s->conv());
                        break;
                    }

                    if (pk->data.dst_id == Conf::instance()->server()->id) {
                        if (pk->data.pid == PID_REG_BKD_REQ) {
                            res = on_regist_terminal_req(s, pk);
                        } else {
                            res = on_pack_handle(s, pk);
                        }
                    } else {
                        res = on_c2s(s, pk);
                    }

                    if (res < 0) {
                        remove_session(s->conv());
                        break;
                    }
                }
            }
        } // while(1);
    }
}


void
adam::kcp::Worker::on_serv_handle(const ::epoll_event& ev) noexcept {
    auto conn = get_serv(((tcp::Connector*)ev.data.ptr)->id());

    if (ev.events & (EPOLLERR | EPOLLHUP)) {
        xWARN("后端连接错误/断开: id = {}, host = {}", conn->id(), conn->host());
        remove_serv(conn);
        return;
    }

    int err = 0;
    if (ev.events & EPOLLOUT) {
        if (conn->state() == tcp::Connector::State::Connecting) {
            socklen_t len = sizeof(err);
            if (::getsockopt(conn->fd(), SOL_SOCKET, SO_ERROR, &err, &len) != 0 || err != 0) {
                xWARN("后端连接失败: {}, err = {}, errstr = {}", conn->to_string(), err, ::strerror(err));
                remove_serv(conn);
                return;
            }

            // 连上成功: 转 Connected
            conn->set_state(tcp::Connector::State::Connected);
            ::epoll_event nev;
            nev.data.ptr = conn.get();
            nev.events = EPOLLIN | EPOLLET | EPOLLOUT;
    
            ASSERT(::epoll_ctl(epfd_, EPOLL_CTL_MOD, conn->fd(), &nev) == 0, "修改后端连接事件失败: id = {}, host = {}, errno = {}, errstr = {}", conn->id(), conn->host(), errno, ::strerror(errno));

            if (conn->regist(Conf::instance()->server()->id, tnow_) < 0) {
                remove_serv(conn);
                return;
            }
        } else if (conn->state() == tcp::Connector::State::Connected) {
            err = conn->send(tnow_);
            if (err < 0) {
                xERROR("发送数据到后端失败: id = {}, host = {}, err = {}", conn->id(), conn->host(), -err);
            }
        }
    }

    if (ev.events & EPOLLIN) {
        thread_local static uint8_t rbuf[TCP_RBUF_SIZE];

        while (1) {
            ssize_t n = ::read(conn->fd(), rbuf, TCP_RBUF_SIZE);
            if (n < 0) {
                err = errno;
                if (err == EWOULDBLOCK || err == EAGAIN) {
                    err = 0;
                } else {
                    xERROR("{} read failed: errno = {}, errstr = {}", conn->host(), err, ::strerror(err));
                }
                break;
            } else if (n == 0) {
                err = EOF;
                break;
            }

            if (conn->input(rbuf, n) != xOK) {
                err = EOF;
                break;
            }
        }

        if (err != 0) {
            remove_serv(conn);
            return;
        }

        alignas(core::Package) static thread_local uint8_t buf[sizeof(core::Package) + (core::PKG_MAX_LEN - core::PKG_HDR_LEN)];
        core::Package* pk = (core::Package*)buf;

        int rc;
        while ((rc = conn->recv(pk, tnow_)) == xOK) {
            switch (pk->data.pid) {
            case PID_PONG:
                on_pong(conn, pk);
                break;

            case PID_REG_BKD_RSP:
                on_regist_backend_rsp(conn, pk);
                break;

            case PID_TER_KIC_NTF:
                on_terminal_kick_notify(conn, pk);
                break;

            case PID_TER_BIND_NTF:
                on_terminal_bind_notify(conn, pk);
                break;

            case PID_TER_UNBD_NTF:
                on_terminal_unbind_notify(conn, pk);
                break;

            case PID_TER_ENT_RSP:
                on_terminal_enter_rsp(conn, pk);
                break;

            default:
                on_s2c(conn, pk);
                break;
            }
        } // while;

        if (rc != xAGAIN) {
            // xERR = 帧非法 → 摘掉这个后端连接
            remove_serv(conn);
            return;
        }
    }
}


void
adam::kcp::Worker::on_ensure_backend(Message* m) noexcept {
    auto* arg = (EnsureBackendArg*)m->arg.ptr;

    // 路由服务集合(本 worker 私有, 随发现结果更新; 服务自报 router=true), 保持升序
    auto rit = std::lower_bound(routers_.begin(), routers_.end(), arg->id);
    bool found = (rit != routers_.end() && *rit == arg->id);
    if (arg->router && !found) {
        routers_.insert(rit, arg->id);
    } else if (!arg->router && found) {
        routers_.erase(rit);
    }

    if (servs_.find(arg->id) == servs_.end()) {
        auto conn = tcp::Connector::create(arg->id, arg->host, arg->pids);
        if (conn) {
            add_backend(conn);
        }
    }

    delete arg;
}


void
adam::kcp::Worker::on_forward_to_session(Message* m) noexcept {
    // 别的 worker 转来的 s2c: arg->raw 是整个 Package 的拷贝, 按 conv 找本 worker 的会话发出
    auto* arg = (ForwardToSessionArg*)m->arg.ptr;
    auto* pk  = (core::Package*)arg->raw;

    auto s = get_session(pk->meta.conv);
    if (s != nullptr && s->send(pk) < 0) {
        xERROR("{} 发送失败", s->to_json());
    }

    delete arg;
}


void
adam::kcp::Worker::update() noexcept {
    auto now = tnow_;

    // 全量遍历: 超时则摘除, 否则推动 KCP。有流量时主循环跑得很快,
    // ts_flush 一到点就被某轮捕获 flush, 延迟接近即时
    for (auto itr = sesss_.begin(); itr != sesss_.end();) {
        auto s = itr->second;
        ++itr;
        if (s->update(now) < 0) {
            remove_session(s->conv());
        }
    }

    // 移除超时的消息发送缓冲, 避免一直重试发送一个发不出去的包导致 sque_ 堆积过大占内存
    // 一趟搞定: 前段过期的 release 掉(不发), 剩下的 sendmmsg, 最后只 erase 一次.
    // 过期段 [0, exp) 与已发段 [exp, exp+nsnd) 连续, 合并成一次 O(n) 移动. b
    size_t exp = 0;
    auto timeout = Conf::instance()->server()->timeout / 2;
    while (exp < dg_que_.size() && dg_que_[exp]->time + timeout < now) {
        dg_pool_.release(dg_que_[exp]);
        ++exp;
    }

    ::mmsghdr msgs[MAX_SEND] {};
    ::iovec iovecs[MAX_SEND] {};
    int nsnd = 0, total = (int)dg_que_.size() - (int)exp;

    while (nsnd < total) {
        int n = total - nsnd;
        n = n > MAX_SEND ? MAX_SEND : n;

        for (int i = 0; i < n; ++i) {
            auto& buf = dg_que_[exp + nsnd + i];
            auto& msg = msgs[i];
            auto& hdr = msg.msg_hdr;

            hdr.msg_name = &buf->addr;
            hdr.msg_namelen = buf->addrlen;
            hdr.msg_iov = &iovecs[i];
            hdr.msg_iovlen = 1;

            iovecs[i].iov_base = buf->buf;
            iovecs[i].iov_len = buf->len;
        }

        int res = ::sendmmsg(ufd_, msgs, n, 0);
        if (res < 0) {
            xERROR("sendmmsg error: errno = {}, errstr = {}", errno, ::strerror(errno));
            break;
        }

        nsnd += res;
        if (res < n) {
            break;
        }
    }

    for (int i = 0; i < nsnd; ++i) {
        dg_pool_.release(dg_que_[exp + i]);
    }
    dg_que_.erase(dg_que_.begin(), dg_que_.begin() + exp + nsnd);

    for (auto itr = servs_.begin(); itr != servs_.end();) {
        auto& conn = itr->second;
        ++itr;
        if (conn->update(now) < 0) {
            remove_serv(conn);
        }
    }
}


void
adam::kcp::Worker::drain_qevent() noexcept {
    constexpr int MQUE_BATCH = 16;
    int i, n;
    Message* ms[MQUE_BATCH];
    while ((n = mque_.try_dequeue_bulk(ms, MQUE_BATCH)) > 0) {
        for (i = 0; i < n; ++i) {
            switch (ms[i]->type) {
            case Message::Type::Stop:
                // 停止事件只为唤醒 epoll(state_ 已在 Worker::stop 里置 Stopping), 消费掉即可;
                // 本轮 on_event_handle 返回后 while(running()) 会自然退出
                break;

            case Message::Type::EnsureBackend:
                on_ensure_backend(ms[i]);
                break;

            case Message::Type::ForwardToSession:
                on_forward_to_session(ms[i]);
                break;

            default:
                xFATAL("无效的 QEvent::Type {}", (int)ms[i]->type);
                break;
            }

            delete ms[i];
        }
    }
}


void
adam::kcp::Worker::on_pong(tcp::Connector::Ptr, core::Package *pk) noexcept {
    if (pk->payload_length() != sizeof(uint64_t)) {
        xERROR("错误的PONG 长度");
    }
}


void
adam::kcp::Worker::on_regist_backend_rsp(tcp::Connector::Ptr conn, core::Package *pk) noexcept {
    if (pk->payload_length() != sizeof(uint32_t)) {
        xERROR("错误的 REGIST_RSP 长度");
        return;
    }
    
    uint32_t res = core::u32_to_le(*((uint32_t*)pk->data.payload));
    if (res == 0) {
        xINFO("注册服务 {} 成功", conn->id());
        conn->set_authed(true);
    } else {
        xERROR("注册服务 {} 失败 {}", conn->id(), res);
    }
}


// 后端(路由服务)要求踢除某终端: 按信封 conv 定位会话, 校验 uid 后摘除
void
adam::kcp::Worker::on_terminal_kick_notify(tcp::Connector::Ptr conn, core::Package *pk) noexcept {
    core::TerminalKickNotify ntf;
    if (ntf.decode(pk->data.payload, pk->payload_length()) != xOK) {
        xERROR("{} KIC_NTF 解析失败", conn->to_string());
        return;
    }

    auto s = get_session(pk->meta.conv);
    if (s == nullptr) {
        return;   // 会话已不在(自然下线/已被摘), 无需处理
    }

    // conv 可能已被新会话复用; uid 对不上说明这是针对旧会话的迟到指令
    if (s->uid() != ntf.uid) {
        xWARN("{} KIC_NTF uid 不匹配: {} != {}", conn->to_string(), s->uid(), ntf.uid);
        return;
    }

    xINFO("{} 被 {} 踢除, code = {}", s->to_json(), conn->to_string(), ntf.code);
    remove_session(pk->meta.conv);
}


// 后端声明已接管该终端: 记入绑定集(该终端下线时需通知它)
void
adam::kcp::Worker::on_terminal_bind_notify(tcp::Connector::Ptr conn, core::Package *pk) noexcept {
    core::TerminalBindNotify ntf;
    if (ntf.decode(pk->data.payload, pk->payload_length()) != xOK) {
        xERROR("{} BIND_NTF 解析失败", conn->to_string());
        return;
    }

    auto s = get_session(pk->meta.conv);
    if (s != nullptr && s->uid() == ntf.uid) {
        s->bind(conn->id());
        return;
    }

    // 竞态闭环: BIND 到达时终端已死 → 立刻回 OFF, 让后端清掉这条幽灵档
    core::TerminalLeaveNotify off;
    off.uid = ntf.uid;

    alignas(core::Package) uint8_t buf[sizeof(core::Package) + core::TerminalLeaveNotify::LEN];
    auto* out = (core::Package*)buf;

    out->meta.len      = core::PKG_HDR_LEN + core::TerminalLeaveNotify::LEN;
    out->meta.conv     = pk->meta.conv;
    out->meta.src_addr = 0;
    out->data.pid      = PID_TER_OFF_NTF;
    out->data.src_id   = Conf::instance()->server()->id;
    out->data.dst_id   = conn->id();
    out->data.seq      = 0;
    off.encode(out->data.payload, core::TerminalLeaveNotify::LEN);

    if (conn->send(*out, tnow_) < 0) {
        xERROR("{} OFF_NTF 回补失败: uid = {}", conn->to_string(), ntf.uid);
    }
}


// 后端不再关心该终端: 移出绑定集
void
adam::kcp::Worker::on_terminal_unbind_notify(tcp::Connector::Ptr conn, core::Package *pk) noexcept {
    core::TerminalUnbindNotify ntf;
    if (ntf.decode(pk->data.payload, pk->payload_length()) != xOK) {
        xERROR("{} UNBD_NTF 解析失败", conn->to_string());
        return;
    }

    auto s = get_session(pk->meta.conv);
    if (s != nullptr && s->uid() == ntf.uid) {
        s->unbind(conn->id());
    }
}


// 路由服务登记应答: 成功即隐含"该后端已绑定本终端"(它不再单发 BIND)
void
adam::kcp::Worker::on_terminal_enter_rsp(tcp::Connector::Ptr conn, core::Package *pk) noexcept {
    core::TerminalEnterRsp rsp;
    if (rsp.decode(pk->data.payload, pk->payload_length()) != xOK) {
        xERROR("{} ENT_RSP 解析失败", conn->to_string());
        return;
    }

    if (rsp.code != 0) {
        xERROR("{} 终端登记失败: uid = {}, code = {}", conn->to_string(), rsp.uid, rsp.code);
        return;
    }

    auto s = get_session(pk->meta.conv);
    if (s != nullptr && s->uid() == rsp.uid) {
        s->bind(conn->id());
    }
}

// 终端鉴权成功: 向路由服务登记。按 uid 取模选实例(升序表保证同一 uid 恒定落点)。
void
adam::kcp::Worker::terminal_enter(Session::Ptr s) noexcept {
    if (routers_.empty()) {
        xWARN("尚未发现路由服务, {} 无法登记", s->to_json());
        return;
    }

    uint32_t rid = routers_[s->uid() % routers_.size()];
    auto sv = get_serv(rid);
    if (sv == nullptr || !sv->is_connected() || !sv->authed()) {
        xWARN("路由服务 {} 不可用, {} 登记失败", rid, s->to_json());
        return;
    }

    core::TerminalEnterReq req;
    req.uid  = s->uid();
    req.conv = s->conv();
    req.ip   = s->remote_addr_u32();
    req.port = ::ntohs(((sockaddr_in*)s->addr())->sin_port);
    req.type = 0;   // TODO: 设备类型待 Eva 的 token 带过来

    alignas(core::Package) uint8_t buf[sizeof(core::Package) + core::TerminalEnterReq::LEN];
    auto* pk = (core::Package*)buf;

    // 信封由网关盖章: 后端会拿 payload 里的 conv/ip 与之交叉校验
    pk->meta.len      = core::PKG_HDR_LEN + core::TerminalEnterReq::LEN;
    pk->meta.conv     = s->conv();
    pk->meta.src_addr = req.ip;
    pk->data.pid      = PID_TER_ENT_REQ;
    pk->data.src_id   = Conf::instance()->server()->id;   // 网关自己发起(后端据此区分终端发起)
    pk->data.dst_id   = rid;
    pk->data.seq      = 0;
    req.encode(pk->data.payload, core::TerminalEnterReq::LEN);

    if (sv->send(*pk, tnow_) < 0) {
        xERROR("{} 向路由服务 {} 登记失败", s->to_json(), rid);
    }
}


// 终端下线: 向绑定集里的每个后端发 OFF, 让它们清掉本终端的档案
void
adam::kcp::Worker::terminal_off(Session::Ptr s) noexcept {
    if (s->binds().empty()) {
        return;
    }

    core::TerminalLeaveNotify ntf;
    ntf.uid = s->uid();

    alignas(core::Package) uint8_t buf[sizeof(core::Package) + core::TerminalLeaveNotify::LEN];
    auto* pk = (core::Package*)buf;

    pk->meta.len      = core::PKG_HDR_LEN + core::TerminalLeaveNotify::LEN;
    pk->meta.conv     = s->conv();
    pk->meta.src_addr = s->remote_addr_u32();
    pk->data.pid      = PID_TER_OFF_NTF;
    pk->data.src_id   = Conf::instance()->server()->id;
    pk->data.seq      = 0;
    ntf.encode(pk->data.payload, core::TerminalLeaveNotify::LEN);

    for (uint32_t sid : s->binds()) {
        auto sv = get_serv(sid);
        if (sv == nullptr || !sv->is_connected() || !sv->authed()) {
            continue;   // 后端已掉线: 它自己的断连清扫会处理这批终端
        }

        pk->data.dst_id = sid;
        if (sv->send(*pk, tnow_) < 0) {
            xERROR("{} 向 {} 发 OFF 失败", s->to_json(), sid);
        }
    }
}


void
adam::kcp::Worker::on_s2c(tcp::Connector::Ptr, core::Package *pk) noexcept {
    auto* wkrs = server_->workers();

    if (pk->meta.conv % wkrs->size() == (size_t)index_) {
        auto s = get_session(pk->meta.conv);

        if (s != nullptr) {
            if (s->send(pk) < 0) {
                xERROR("{} 发送失败", s->to_json());
            }
        }
    } else {
        (*wkrs)[pk->meta.conv % wkrs->size()]->notify(
            new Message(Message::Type::ForwardToSession,
                new ForwardToSessionArg((uint8_t*)pk, sizeof(core::Package) + pk->payload_length())
            )
        );
    }
}


int
adam::kcp::Worker::on_regist_terminal_req(Session::Ptr s, core::Package *in) noexcept {
    // REGIST_REQ 的 payload = sealedbox 密封的 AccessToken 明文(116) + sealedbox 头(48)
    constexpr int REGIST_PAYLOAD_LEN = core::RegistTerminalReq::LEN + 48;

    if (s->authed()) {
        return xDUP;
    }

    if ((int)in->payload_length() != REGIST_PAYLOAD_LEN) {
        return xERR_PK_LEN;
    }

    // 1. 服务端私钥解密 → 116 字节明文
    uint8_t plain[REGIST_PAYLOAD_LEN];
    size_t  plen = sizeof(plain);
    if (utils::sealedbox_decrypt(in->data.payload, in->payload_length(), plain, &plen, Conf::instance()->x25519_sk(), Conf::instance()->x25519_pk())) {
        return xERR_PK_DEC;
    }
    if (plen != (size_t)core::RegistTerminalReq::LEN) {
        return xERR_PK_DEC;
    }

    // 2. 显式解出 token(小端, 不做内存覆盖)
    core::RegistTerminalReq req;
    if (req.decode(plain, plen) != xOK) {
        return xERR_PK_LEN;
    }

    // 3. 有效期 / conv / user 校验
    if ((uint64_t)::time(nullptr) > req.expire) {
        return xERR_TOKEN_EXP;
    }

    if (req.conv != s->conv()) {
        return xERR_TOKEN_CONV;
    }

    if (req.uid != in->data.src_id) {
        return xERR_TOKEN_USER;
    }

    // 4. 校验登录服签名: RA 签的是明文前 ACCESS_TOKEN_SIGNED_LEN(52) 字节
    if (utils::ed25519_verify(req.sign, plain, core::RegistTerminalReq::SIGNED_LEN, Conf::instance()->ed25519_pk()) != 0) {
        return xERR_TOKEN_VER;
    }

    // 4. 计算 ECDH 共享密钥并派生 AES Key
    uint8_t tmppk[utils::X25519_KEY_LEN];
    uint8_t tmpsk[utils::X25519_KEY_LEN];
    ASSERT(utils::x25519_keygen(tmppk, tmpsk) == xOK, "生成临时 x25519 密钥对失败");

    uint8_t rxkey[utils::SESSION_KEY_LEN];
    uint8_t txkey[utils::SESSION_KEY_LEN];

    if (utils::x25519_kx_server(rxkey, txkey, tmppk, tmpsk, req.cli_pk) != xOK) {
        return xERR_X25519_KX;
    }

    // 5. 激活 Session 加密
    s->set_key(txkey, rxkey);

    // 6. 构造 REGIST_RSP 回包(明文: 客户端此刻还没派生出会话密钥, 需要 tmppk 才能派生)
    core::RegistTerminalRsp rsp;
    ::memcpy(rsp.PK, tmppk, sizeof(rsp.PK));

    alignas(core::Package) uint8_t buf[sizeof(core::Package) + core::RegistTerminalRsp::LEN] = {0};
    auto* out = (core::Package*)buf;
    out->meta.len    = core::PKG_HDR_LEN + core::RegistTerminalRsp::LEN;
    out->data.pid    = PID_TER_REG_RSP;
    out->data.src_id = Conf::instance()->server()->id;
    out->data.dst_id = req.uid;
    rsp.encode(out->data.payload, core::RegistTerminalRsp::LEN);

    int res = s->send(out);
    s->set_uid(req.uid);

    // 鉴权通过 → 向路由服务登记(顶号仲裁/下线通知的前提)
    terminal_enter(s);

    return res;
}


int
adam::kcp::Worker::on_c2s(Session::Ptr s, core::Package *pk) noexcept {
    if (!s->authed()) {
        return xERR_NOT_AUTH;
    }

    auto sv = get_serv(pk->data.dst_id);
    if (sv == nullptr) {
        xERROR("{} 转包: invalid dst_id [{}]", s->to_json(), pk->data.dst_id);
        return xERR_PK_DST;
    }

    // 目标后端未声明该 PID: 客户端协议错(或版本错位), 判死并留下线索
    if (!sv->pid_has(pk->data.pid)) {
        xERROR("{} 转包: {} 未受理 PID [{}]", s->to_json(), sv->id(), pk->data.pid);
        return xERR_PK_PID;
    }

    if (!sv->is_connected() || !sv->authed()) {
        xWARN("后台服务 {} 还未鉴权完成", sv->id());
        return xOK;
    }

    // pk 已带 meta.conv/src_addr(Session::recv 时本地合成), 整帧转发给后端
    if (sv->send(*pk, tnow_) < 0) {
        xERROR("{} 转发消息至 {} 失败: {}", s->to_json(), sv->id(), errno);
    }

    return xOK;
}


int
adam::kcp::Worker::on_pack_handle(Session::Ptr s, core::Package* pk) noexcept {
    auto handler = server_->get_handler(pk->data.pid);
    return handler == nullptr ? -1 : handler(s, pk);
}
