#include "kcp/worker.hpp"
#include "kcp/server.hpp"
#include "tcp/connector.hpp"


static constexpr int MAX_EVENTS    = 64;
static constexpr int INTERVAL_MS   = 10;
static constexpr int TCP_RBUF_SIZE = 8 * 1024;


adam::kcp::Worker::Worker(Server* s, int idx) noexcept
    : server_(s)
    , index_(idx) {
    ASSERT(server_ != nullptr && idx >= 0, "invalid server or idx");
    event_ = server_->event();

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

    for (auto* sb: sque_) {
        sb_pool_.release(sb);
    }
    sque_.clear();

    evque_.clear([](core::QEvent* qe) {
        if (qe->type == core::QEvent::Type::AddServ) {
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
    auto* svr = s->server();
    auto* sb  = svr->sb_pool_.acquire(s->addr(), s->addrlen(), buf, len, svr->tnow());
    svr->sque_.emplace_back(sb);
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
        event_->on_sess_disconnected(sess);
    }
}


void
adam::kcp::Worker::remove_serv(tcp::Connector::Ptr c) noexcept {
    ASSERT(::epoll_ctl(epfd_, EPOLL_CTL_DEL, c->fd(), nullptr) == 0, "epoll_ctl failed: errno = {}, errstr = {}", errno, ::strerror(errno));
    server_->event()->on_serv_disconnected(c);
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
    evq_workring_.store(false);
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

                    if (pk->data.dst_id == Conf::instance()->server()->id) {
                        if (pk->data.id == PKID_REGIST_REQ) {
                            res = on_regist_req(s, pk);
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
            switch (pk->data.id) {
            case PKID_PONG:
                on_pong(conn, pk);
                break;

            case PKID_REGIST_RSP:
                on_regist_rsp(conn, pk);
                break;

            default:
                on_s2c(conn, pk);   // 本地发 / 跨 worker 转(内部拷 KcpSendArg.raw)
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
adam::kcp::Worker::on_new_serv(core::QEvent* qe) noexcept {
    auto* arg = (core::AddServArg*)qe->arg.ptr;

    if (servs_.find(arg->id) == servs_.end()) {
        auto conn = tcp::Connector::create(arg->id, arg->host);
        if (conn) {
            add_serv(conn);
        }
    }

    delete arg;
}


void
adam::kcp::Worker::on_user_send(core::QEvent* qe) noexcept {
    // 别的 worker 转来的 s2c: arg->raw 是整个 Package 的拷贝, 按 conv 找本 worker 的会话发出
    auto* arg = (core::KcpSendArg*)qe->arg.ptr;
    auto* pk  = (core::Package*)arg->raw;

    auto s = get_session(pk->meta.conv);
    if (s != nullptr && s->send(pk) < 0) {
        xERROR("{} 发送失败", s->to_json());
    }

    ::mi_free(arg->raw);
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
adam::kcp::Worker::drain_qevent() noexcept {
    constexpr int EVQUE_BATCH = 16;
    int i, n;
    core::QEvent* qes[EVQUE_BATCH];
    while ((n = evque_.try_dequeue_bulk(qes, EVQUE_BATCH)) > 0) {
        for (i = 0; i < n; ++i) {
            switch (qes[i]->type) {
            case core::QEvent::Type::Stop:
                // 停止事件只为唤醒 epoll(state_ 已在 Worker::stop 里置 Stopping), 消费掉即可;
                // 本轮 on_event_handle 返回后 while(running()) 会自然退出。
                break;

            case core::QEvent::Type::AddServ:
                on_new_serv(qes[i]);
                break;

            case core::QEvent::Type::KcpSend:
                on_user_send(qes[i]);
                break;

            default:
                xFATAL("无效的 QEvent::Type {}", (int)qes[i]->type);
                break;
            }

            delete qes[i];
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
adam::kcp::Worker::on_regist_rsp(tcp::Connector::Ptr conn, core::Package *pk) noexcept {
    if (pk->payload_length() != sizeof(uint32_t)) {
        xERROR("错误的 REGIST_RSP 长度");
        return;
    }
    
    uint32_t res = ::ntohl(*((uint32_t*)pk->data.payload));
    if (res == 0) {
        xINFO("注册服务 {} 成功", conn->id());
        conn->set_authed(true);
    } else {
        xERROR("注册服务 {} 失败 {}", conn->id(), res);
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
            new core::QEvent(core::QEvent::Type::KcpSend, new core::KcpSendArg((uint8_t*)pk, sizeof(core::Package) + pk->payload_length()))
        );
    }
}


int
adam::kcp::Worker::on_regist_req(Session::Ptr s, core::Package *in) noexcept {
    // REGIST_REQ 的 payload = sealedbox 密封的 AccessToken 明文(116) + sealedbox 头(48)
    constexpr int REGIST_PAYLOAD_LEN = core::ACCESS_TOKEN_LEN + 48;

    if (s->authed()) {
        return xDUP;
    }

    if ((int)in->payload_length() != REGIST_PAYLOAD_LEN) {
        return xERR_PK_LEN;
    }

    // 1. 服务端私钥解密 → 116 字节明文
    uint8_t plain[core::ACCESS_TOKEN_LEN];
    size_t  plen = sizeof(plain);
    if (utils::sealedbox_decrypt(in->data.payload, in->payload_length(), plain, &plen,
                                 Conf::instance()->x25519_sk(), Conf::instance()->x25519_pk())) {
        return xERR_PK_DEC;
    }
    if (plen != core::ACCESS_TOKEN_LEN) {
        return xERR_PK_DEC;
    }

    // 2. 显式解出 token(小端, 不做内存覆盖)
    core::AccessToken token;
    core::token_decode(plain, &token);

    // 3. 有效期 / conv / user 校验
    if ((uint64_t)::time(nullptr) > token.expire) {
        return xERR_TOKEN_EXP;
    }

    if (token.conv != s->conv()) {
        return xERR_TOKEN_CONV;
    }

    if (token.user_id != in->data.src_id) {
        return xERR_TOKEN_USER;
    }

    const uint32_t n = server_->workers()->size();
    if (token.conv % n != token.user_id % n) {
        xWARN("conv 不变量违反: conv % N= {} != user_id % N= {}, 拒绝(检查 RA 的 conv 算法或 N 配置)", token.conv % n, token.user_id % n);
        return xERR_TOKEN_CONV;   // 或专门错误码
    }

    // 4. 校验登录服签名: RA 签的是明文前 ACCESS_TOKEN_SIGNED_LEN(52) 字节
    if (utils::ed25519_verify(token.sign, plain, core::ACCESS_TOKEN_SIGNED_LEN, Conf::instance()->ed25519_pk()) != 0) {
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

    // 6. 构造 REGIST_RSP 回包(明文: 客户端此刻还没派生出会话密钥, 需要 tmppk 才能派生)
    alignas(core::Package) uint8_t buf[sizeof(core::Package) + utils::X25519_KEY_LEN] = {0};
    auto* out = (core::Package*)buf;
    out->meta.len    = core::PKG_HDR_LEN + utils::X25519_KEY_LEN;
    out->data.id     = PKID_REGIST_RSP;
    out->data.src_id = Conf::instance()->server()->id;
    out->data.dst_id = token.user_id;
    ::memcpy(out->data.payload, tmppk, utils::X25519_KEY_LEN);

    int res = s->send(out);        // authed 尚为 false → 明文发出
    s->set_user_id(token.user_id); // 之后才 authed, 后续消息才加密
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
    auto handler = server_->get_handler(pk->data.id);
    return handler == nullptr ? -1 : handler(s, pk);
}