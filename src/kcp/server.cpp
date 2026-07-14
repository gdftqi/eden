#include "kcp/server.hpp"
#include "tcp/connector.hpp"
#include "core/error.hpp"


static constexpr int MAX_EVENTS    = 64;
static constexpr int INTERVAL_MS   = 10;
static constexpr int TCP_RBUF_SIZE = 8 * 1024;


typhon::kcp::Server::Server(int idx, const char* host, IEvent* ev) noexcept
    : idx_(idx)
    , event_(ev)
    , host_(host) {

    Conf::instance()->check();
    ASSERT(host_.size() > 0 && event_ != nullptr, "invalid host or IEvent instance");

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

    ufd_ = core::udp_bind(host_, Conf::instance()->sndbuf(), Conf::instance()->rcvbuf());
    ASSERT(ufd_ != core::INVALID_SOCKET, "创建 udp fd 失败: errno = {}, errstr = {}", errno, ::strerror(errno));
    ::ikcp_allocator(::mi_malloc, ::mi_free);
}


typhon::kcp::Server::~Server() noexcept {
    for (int i = 0; i < MAX_RECV; ++i) {
        ::mi_free(riovecs_[i].iov_base);
    }
}


void
typhon::kcp::Server::run() noexcept {
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
    users_.clear();
    servs_.clear();

    for (auto* sb: sque_) {
        sb_pool_.release(sb);
    }
    sque_.clear();

    evque_.clear([](core::QEvent* qe) {
        if (qe->qe_type == core::QEvent::Type::AddServ) {
            ::mi_free(qe->qe_data.ptr);
        }
        delete qe; 
    });

    release();
    state_.store(core::State::Stopped);
}


int
typhon::kcp::Server::output(const char *buf, int len, IKCPCB* kcpcb) noexcept {
    auto s   = (Session*)kcpcb->user;
    auto svr = s->server();
    auto sb  = svr->sb_pool_.acquire(s->addr(), s->addrlen(), buf, len, svr->tnow());
    svr->sque_.emplace_back(sb);
    return 0;
}


void
typhon::kcp::Server::init() noexcept {
    epfd_ = ::epoll_create1(0);
    ASSERT(epfd_ != core::INVALID_SOCKET, "创建 epoll fd 失败: errno = {}, errstr = {}", errno, ::strerror(errno));

    evfd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT(evfd_ != core::INVALID_SOCKET, "创建 停止事件 fd 失败: errno = {}, errstr = {}", errno, ::strerror(errno));

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
typhon::kcp::Server::release() noexcept {
    if (epfd_ != core::INVALID_SOCKET) {
        ::close(epfd_);
        epfd_ = core::INVALID_SOCKET;
    }

    if (evfd_ != core::INVALID_SOCKET) {
        ::close(evfd_);
        evfd_ = core::INVALID_SOCKET;
    }

    if (ufd_ != core::INVALID_SOCKET) {
        ::close(ufd_);
        ufd_ = core::INVALID_SOCKET;
    }
}


void
typhon::kcp::Server::remove_serv(tcp::Connector::Ptr c) noexcept {
    ASSERT(::epoll_ctl(epfd_, EPOLL_CTL_DEL, c->fd(), nullptr) == 0, "epoll_ctl failed: errno = {}, errstr = {}", errno, ::strerror(errno));
    event_->on_serv_disconnected(c);
    servs_.erase(c->id());
}


void
typhon::kcp::Server::on_event_handle(const ::epoll_event& ev) noexcept {
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

    evq_wkring_.store(true);
    drain_qevent();

    evq_wkring_.store(false);
    drain_qevent();
}


void
typhon::kcp::Server::on_udp_handle(const ::epoll_event& ev) noexcept {
    thread_local static uint8_t rbuf[core::PKG_MAX_LEN + core::PKX_HDR_LEN];

    // ufd_ 是 EPOLLET, 且和后端 Connector fd / 控制 eventfd 共用同一个 epoll 线程.
    // MAX_ROUND 只是防洪峰的安全阀(没吃完的包留在内核缓冲区, ET 下一有新包到达即自愈,不会丢),
    // 不是常规吞吐限流。正常流量下应远打不到这个 cap, 真正起作用的场景是攻击/洪峰.
    //
    // 取值公式:
    //   MAX_ROUND = ceil((总QPS / worker数) * INTERVAL_MS(10ms) * 余量系数(5~10) / MAX_RECV(128))
    constexpr int MAX_ROUND = 16;

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
                    // TODO: 记录攻击行为, 并且封禁对端 IP 一段时间
                    xWARN("UDP truncated from {} dropped", core::sockaddr_to_string((sockaddr*)rmsgs_[i].msg_hdr.msg_name));
                    continue;
                }

                auto& msg = rmsgs_[i];
                if (msg.msg_len < core::ENVELOPE_MAC_LEN + core::KCP_HDR_LEN) {
                    // 如果长KCP协议头 + SIPHASH 长度, 说明这个包不合法
                    // TODO: 记录攻击行为, 并且封禁对端 IP 一段时间
                    continue;
                }

                auto  hdr    = &rmsgs_[i].msg_hdr;
                auto* raw    = (uint8_t*)hdr->msg_iov[0].iov_base;
                auto  msglen = (long)msg.msg_len;

                auto conv = Session::getconv(raw, msglen);
                if (conv == 0) {
                    // TODO: 记录攻击行为, 并且封禁对端 IP 一段时间
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

                core::PK<core::Host> pk;
                uint8_t* pkbuf = rbuf + core::PKX_HDR_LEN;
                while (true) {
                    res = s->recv(&pk, pkbuf, core::PKG_MAX_LEN);
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

                    if (pk->id == PKID_REGIST_REQ) {
                        res = on_regist_req(s, pk);
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
typhon::kcp::Server::on_serv_handle(const ::epoll_event& ev) noexcept {
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

            if (conn->regist(Conf::instance()->id(), tnow_) < 0) {
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

        core::PKx<core::Host> pkx;
        while (conn->recv(&pkx, tnow_) == xOK) {
            switch (pkx.pk()->id) {
            case PKID_PONG:
                on_pong(conn, pkx);
                break;

            case PKID_REGIST_RSP:
                on_regist_rsp(conn, pkx);
                break;

            default:
                on_s2c(conn, pkx);
                break;
            }
        } // while;
    }
}


void
typhon::kcp::Server::on_new_serv(core::QEvent* qe) noexcept {
    auto* arg = (core::AddServArg*)qe->qe_data.ptr;

    if (servs_.find(arg->id) == servs_.end()) {
        auto conn = tcp::Connector::create(arg->id, arg->host);
        if (conn) {
            add_serv(conn);
        }
    }

    delete arg;
}


void
typhon::kcp::Server::on_user_send(core::QEvent* qe) noexcept {
    auto* arg = (core::KcpSendArg*)qe->qe_data.ptr;

    core::PK<core::Host> pk(arg->raw, arg->len);
    auto s = get_user(pk->dst_id);
    if (s != nullptr) {
        int res = s->send(pk);
        if (res != xOK) {
            xERROR("{} 发送消息失败: {}", s->user_id(), res);
        }
    }

    ::mi_free(arg->raw);
    delete arg;
}


void
typhon::kcp::Server::update() noexcept {
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
    auto timeout = Conf::instance()->timeout() / 2;
    while (exp < sque_.size() && sque_[exp]->time + timeout < now) {
        sb_pool_.release(sque_[exp]);
        ++exp;
    }

    ::mmsghdr msgs[MAX_SEND] {};
    ::iovec iovecs[MAX_SEND] {};
    int nsnd = 0, total = (int)sque_.size() - (int)exp;

    while (nsnd < total) {
        int n = total - nsnd;
        n = n > MAX_SEND ? MAX_SEND : n;

        for (int i = 0; i < n; ++i) {
            auto& buf = sque_[exp + nsnd + i];
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
        sb_pool_.release(sque_[exp + i]);
    }
    sque_.erase(sque_.begin(), sque_.begin() + exp + nsnd);

    for (auto itr = servs_.begin(); itr != servs_.end();) {
        auto& conn = itr->second;
        ++itr;
        if (conn->update(now) < 0) {
            remove_serv(conn);
        }
    }
}


void
typhon::kcp::Server::on_pong(tcp::Connector::Ptr, core::PKx<core::Host> &pkx) noexcept {
    if (pkx.payload_len() != sizeof(uint64_t)) {
        xERROR("错误的PONG 长度");
    }
}


void
typhon::kcp::Server::on_regist_rsp(tcp::Connector::Ptr conn, core::PKx<core::Host> &pkx) noexcept {
    if (pkx.payload_len() != sizeof(uint32_t)) {
        xERROR("错误的 REGIST_RSP 长度");
        return;
    }
    
    uint32_t res = ::ntohl(*((uint32_t*)pkx.pk()->payload));
    if (res == 0) {
        xINFO("注册服务 {} 成功", conn->id());
        conn->set_authed(true);
    } else {
        xERROR("注册服务 {} 失败 {}", conn->id(), res);
    }
}


void
typhon::kcp::Server::on_s2c(tcp::Connector::Ptr, core::PKx<core::Host> &pkx) noexcept {
    core::PK<core::Host> pk(pkx.pk(), pkx.payload_len() + core::PKG_HDR_LEN);

    auto user = get_user(pkx.pk()->dst_id);
    if (user != nullptr) {
        // 直接用 pkx.pk()(指向 Connector rbuf_): Session::send 把密文+tag 加密输出到它
        // 自己的发送暂存 buf, 不原地改 rbuf_, 故不会踩 rbuf_ 里粘在后面的下一个包.
        if (user->send(pk) < 0) {
            xERROR("{} 发送失败", user->to_json());
        }
    } else {
        Pool()[pk->dst_id % Pool().size()]->notify(
            new core::QEvent(core::QEvent::Type::KcpSend, new core::KcpSendArg(pk.raw(), pk.size()))
        );
    }
}


int
typhon::kcp::Server::on_regist_req(Session::Ptr s, core::PK<core::Host>& in) noexcept {
    constexpr int REGIST_PKG_LEN = core::PKG_HDR_LEN + (int)sizeof(core::AccessToken) + 48;

    if (s->authed()) {
        return xDUP;
    }

    if (in.size() != REGIST_PKG_LEN) {
        return xERR_PK_LEN;
    }

    if (in->dst_id != Conf::instance()->id()) {
        return xERR_PKT_DST;
    }

    size_t plen = in.payload_len();
    core::AccessToken token;
    size_t tklen = sizeof(token);
    
    // 1. 使用服务端私钥解密 Token
    if (utils::sealedbox_decrypt(in->payload, plen, (uint8_t*)&token, &tklen, Conf::instance()->x25519_sk(), Conf::instance()->x25519_pk())) {
        return xERR_PK_DEC;
    }

    if (tklen != sizeof(token)) {
        return xERR_PK_DEC;
    }

    // 2. 检查有效期
    if ((uint64_t)::time(nullptr) > token.expire) {
        return xERR_TOKEN_EXP;
    }

    if (token.conv != s->conv()) {
        return xERR_TOKEN_CONV;
    }

    if (token.user_id != in->src_id) {
        return xERR_TOKEN_USER;
    }

    const uint32_t n = Pool().size();
    if (token.conv % n != token.user_id % n) {
        xWARN("conv 不变量违反: conv % N= {} != user_id % N= {}, 拒绝(检查 RA 的 conv 算法或 N 配置)", token.conv % n, token.user_id % n);
        return xERR_TOKEN_CONV;   // 或专门错误码
    }

    // 3. 校验登录服签名
    if (utils::ed25519_verify(token.sign, (uint8_t*)&token, offsetof(core::AccessToken, sign), Conf::instance()->ed25519_pk()) != 0) {
        return xERR_TOKEN_VER;
    }

    // 4. 计算 ECDH 共享密钥并派生 AES Key
    uint8_t tmppk[utils::X25519_KEY_LEN];
    uint8_t tmpsk[utils::X25519_KEY_LEN];
    ASSERT(utils::x25519_keygen(tmppk, tmpsk) == xOK, "生成临时 x25519 密钥对失败");

    uint8_t rxkey[utils::SESSION_KEY_LEN];
    uint8_t txkey[utils::SESSION_KEY_LEN];

    if (utils::x25519_kx_server(rxkey, txkey, tmppk, tmpsk, token.cli_pk) != xOK) {
        return xERR_X25519_KX;
    }

    // 5. 激活 Session 加密
    s->set_key(txkey, rxkey);

    // 6. 构造回包
    auto out = core::PK<core::Host>::create(PKID_REGIST_RSP, Conf::instance()->id(), token.user_id, tmppk, utils::X25519_KEY_LEN);
    int res = s->send(out);
    s->set_user_id(token.user_id);

    // 7. 踢除已在线的用户
    auto oitr = users_.find(s->user_id());
    if (oitr != users_.end()) {
        remove_session(oitr->second->conv());
    }

    // 8. 添加新用户
    users_.emplace(s->user_id(), s);
    event_->on_user_connected(s);
    core::PK<core::Host>::release(out);
    return res;
}


int
typhon::kcp::Server::on_c2s(Session::Ptr s, core::PK<core::Host> &pk) noexcept {
    if (!s->authed()) {
        return xERR_NOT_AUTH;
    }

    auto sv = get_serv(pk->dst_id);
    if (sv == nullptr) {
        xERROR("{} 转包: invalid dst_id [{}]", s->to_json(), pk->dst_id);
        return xERR_PKT_DST;
    }

    if (!sv->is_connected() || !sv->authed()) {
        xWARN("后台服务 {} 还未鉴权完成", sv->id());
        return xOK;
    }

    core::PKx<core::Host> pkx(pk.raw() - core::PKX_HDR_LEN, pk.size() + core::PKX_HDR_LEN);
    pkx->len = (uint16_t)(core::PKX_HDR_LEN + pk.size());
    pkx->src_addr = s->remote_addr_u32();

    if (sv->send(pkx, tnow_) < 0) {
        xERROR("{} 转发消息至 {} 失败: {}", s->to_json(), sv->id(), errno);
    }

    return xOK;
}


void
typhon::kcp::Server::drain_qevent() noexcept {
    constexpr int EVQUE_BATCH = 16;
    int i, n;
    core::QEvent* qes[EVQUE_BATCH];
    while ((n = evque_.try_dequeue_bulk(qes, EVQUE_BATCH)) > 0) {
        for (i = 0; i < n; ++i) {
            switch (qes[i]->qe_type) {
            case core::QEvent::Type::AddServ:
                on_new_serv(qes[i]);
                break;

            case core::QEvent::Type::KcpSend:
                on_user_send(qes[i]);
                break;

            default:
                xFATAL("无效的 QEvent::Type {}", (int)qes[i]->qe_type);
                break;
            }

            delete qes[i];
        }
    }
}