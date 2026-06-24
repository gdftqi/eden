#include "kcp/server.hpp"
#include "tcp/connector.hpp"
#include "core/error.hpp"
#include "typhon.hpp"


static constexpr int MAX_EVENTS    = 64;
static constexpr int INTERVAL_MS   = 10;
static constexpr int TCP_RBUF_SIZE = 8 * 1024;


typhon::kcp::Server::Server(const char* host, IEvent* ev, void* onwer) noexcept
    : onwer_(onwer)
    , event_(ev)
    , host_(host)
    , desc_(std::format("[{}:{}]", Conf::instance()->id(), host_)) {

    ASSERT(host_.size() > 0 && ev != nullptr, "invalid host or IEvent instance");

    xkcp_init(
        Server::output, 
        Conf::instance()->x25519_pk(),
        Conf::instance()->x25519_sk(),
        Conf::instance()->ed25519_pk(),
        Conf::instance()->siphash(),
        Conf::instance()->sndwnd(),
        Conf::instance()->rcvwnd(),
        Conf::instance()->interval(),
        Conf::instance()->resend(),
        Conf::instance()->timeout()
    );

    for (int i = 0; i < MAX_RECV; ++i) {
        auto hdr = &rmsgs_[i].msg_hdr;
        hdr->msg_iov = &riovecs_[i];
        hdr->msg_iovlen = 1;
        hdr->msg_name = &raddrs_[i];
        hdr->msg_namelen = sizeof(raddrs_[i]);

        riovecs_[i].iov_len = XKCP_LINK_MTU;
        riovecs_[i].iov_base = ::mi_malloc(XKCP_LINK_MTU);
        ASSERT(riovecs_[i].iov_base != nullptr, "failed to allocate memory for riovec"); 
    }

    ufd_ = core::udp_bind(host_, Conf::instance()->sndbuf(), Conf::instance()->rcvbuf());
    ASSERT(ufd_ != core::INVALID_SOCKET, "创建 udp fd 失败: errno = {}, errstr = {}", errno, ::strerror(errno));
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

    users_.clear();
    servs_.clear();

    for (auto* sb: sque_) {
        sb_pool_.release(sb);
    }
    sque_.clear();
    tnow_ = 0;

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
typhon::kcp::Server::output(const uint8_t *buf, int len, XKCPCB *kcp) noexcept {
    auto* s  = (Session*)kcp->user;
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

    event_->on_init(this);
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

    event_->on_stopped(this);
}


void
typhon::kcp::Server::remove_serv(tcp::Connector* c) noexcept {
    ASSERT(::epoll_ctl(epfd_, EPOLL_CTL_DEL, c->fd(), nullptr) == 0, "epoll_ctl failed: errno = {}, errstr = {}", errno, ::strerror(errno));
    typhon::Server* s = (typhon::Server*)onwer_;
    s->notify_serv_disconnected(c->id());
    servs_.erase(c->id());
}


void
typhon::kcp::Server::on_event_handle(const ::epoll_event& ev) noexcept {
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

    evflag_.store(false, std::memory_order_relaxed);

    constexpr int EVQUE_BATCH = 16;
    int i, n;
    core::QEvent* qes[EVQUE_BATCH];
    while ((n = evque_.try_dequeue_bulk(qes, EVQUE_BATCH)) > 0) {
        for (i = 0; i < n; ++i) {
            if (qes[i]->qe_type == core::QEvent::Type::AddServ) {
                on_new_serv(qes[i]);
            }
            delete qes[i];
        }
    }
}


void
typhon::kcp::Server::on_udp_handle(const ::epoll_event& ev) noexcept {
    thread_local static uint8_t rbuf[core::PKG_MAX_LEN + core::PKX_HDR_LEN];

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
        int i, n;
        constexpr int MAX_ROUND = 8;
        int round = 0;
        while (round++ < MAX_ROUND) {
            for (i = 0; i < MAX_RECV; ++i) {
                riovecs_[i].iov_len = XKCP_LINK_MTU;
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
                    // 判断 UDP 包被截断了.
                    // 内核收到的包长度超过了 UDP_MTU, 后面的数据被丢弃.
                    // 如果出现这种情况, 一定是攻击行为.
                    xWARN("UDP truncated from {} dropped", core::sockaddr_to_string((sockaddr*)rmsgs_[i].msg_hdr.msg_name));
                    continue;
                }

                auto& msg = rmsgs_[i];
                if (msg.msg_len < crypto_shorthash_BYTES + XKCP_HDR_LEN) {
                    // 如果长KCP协议头 + SIPHASH 长度, 说明这个包不合法.
                    continue;
                }

                auto  hdr    = &rmsgs_[i].msg_hdr;
                auto* raw    = (uint8_t*)hdr->msg_iov[0].iov_base;
                auto  msglen = (long)msg.msg_len;

                // conv 在信封之后(偏移 8); 信封由 XDP 校验, xkcp_input 内部再剥这 8B
                auto conv   = xkcp_getconv(raw);
                auto sess   = get_session(conv);
                bool is_new = false;
                if (sess == nullptr) {
                    is_new = true;
                    sess = Session::create(conv, this, hdr->msg_name, hdr->msg_namelen);
                }

                if (sess->input(raw, msglen, hdr->msg_name, hdr->msg_namelen) != 0) {
                    continue;
                }

                if (is_new && add_session(conv, sess) != 0) {
                    continue;
                }

                core::PK<core::Host> pk;
                uint8_t* pkbuf = rbuf + core::PKX_HDR_LEN;
                while (true) {
                    res = sess->recv(&pk, pkbuf, core::PKG_MAX_LEN);
                    if (res == xAGAIN) {
                        // 没有更多消息了
                        break;
                    } else if (res < 0) {
                        // 协议错误
                        remove_session(sess->conv());
                        break;
                    }

                    res = on_c2s(sess, pk);
                    if (res < 0) {
                        remove_session(sess->conv());
                        break;
                    }
                }
            }
        } // while(1);
    }
}


void
typhon::kcp::Server::on_serv_handle(const ::epoll_event& ev) noexcept {
    auto* conn = (tcp::Connector*)ev.data.ptr;

    if (ev.events & (EPOLLERR | EPOLLHUP)) {
        xERROR("后端连接错误/断开: id = {}, host = {}", conn->id(), conn->host());
        remove_serv(conn);
        return;
    }

    int err = 0;
    if (ev.events & EPOLLOUT) {
        if (conn->state() == tcp::Connector::State::Connecting) {
            socklen_t len = sizeof(err);
            if (::getsockopt(conn->fd(), SOL_SOCKET, SO_ERROR, &err, &len) != 0 || err != 0) {
                xERROR("后端连接失败: {}, err = {}, errstr = {}", conn->to_string(), err, ::strerror(err));
                remove_serv(conn);
                return;
            }

            // 连上成功: 转 Connected
            conn->set_state(tcp::Connector::State::Connected);
            ::epoll_event nev;
            nev.data.ptr = conn;
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

    if (servs_.find(arg->id) != servs_.end()) {
        // 如果存在, 直接返回
        ::mi_free(arg);
        return;
    }

    auto conn = tcp::Connector::create(arg->id, arg->host);
    if (conn) {
        add_serv(conn);
    }
    ::mi_free(arg);
}


void
typhon::kcp::Server::update() noexcept {
    auto now = tnow_;

    // 全量遍历: 超时则摘除, 否则推动 KCP。有流量时主循环跑得很快,
    // ts_flush 一到点就被某轮捕获 flush, 延迟接近即时
    for (auto itr = users_.begin(); itr != users_.end();) {
        auto s = itr->second;
        if (s->update(now) < 0) {
            users_.erase(itr++);
            event_->on_disconnected(s);
        } else {
            ++itr;
        }
    }

    // 移除超时的消息发送缓冲, 避免一直重试发送一个发不出去的包导致 sque_ 堆积过大占内存
    // 一趟搞定: 前段过期的 release 掉(不发), 剩下的 sendmmsg, 最后只 erase 一次.
    // 过期段 [0, exp) 与已发段 [exp, exp+nsnd) 连续, 合并成一次 O(n) 移动. b
    size_t exp = 0;
    auto timeout = (uint64_t)Conf::instance()->timeout() / 2;
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
        if (conn->update(now) < 0) {
            ASSERT(::epoll_ctl(epfd_, EPOLL_CTL_DEL, conn->fd(), nullptr) == 0, "epoll_ctl failed: errno = {}, errstr = {}", errno, ::strerror(errno));
            ((typhon::Server*)onwer_)->notify_serv_disconnected(conn->id());
            servs_.erase(itr++);
        } else {
            ++itr;
        }
    }
}


void
typhon::kcp::Server::on_pong(tcp::Connector*, core::PKx<core::Host> &pkx) noexcept {
    if (pkx.plen() != sizeof(uint64_t)) {
        xERROR("错误的PONG 长度");
    }
}


void
typhon::kcp::Server::on_regist_rsp(tcp::Connector* conn, core::PKx<core::Host> &pkx) noexcept {
    if (pkx.plen() != sizeof(uint32_t)) {
        xERROR("错误的 REGIST_RSP 长度");
        return;
    }
    
    uint32_t res = ntohl(*((uint32_t*)pkx.pk()->payload));
    if (res == 0) {
        xINFO("注册服务 {} 成功", conn->id());
        conn->set_authed(true);
    } else {
        xERROR("注册服务 {} 失败 {}", conn->id(), res);
    }
}


void
typhon::kcp::Server::on_s2c(tcp::Connector*, core::PKx<core::Host> &pkx) noexcept {
    auto s = get_session(pkx.pk()->dst_id);
    if (s != nullptr) {
        // 直接用 pkx.pk()(指向 Connector rbuf_): Session::send 把密文+tag 加密输出到它
        // 自己的发送暂存 buf, 不原地改 rbuf_, 故不会踩 rbuf_ 里粘在后面的下一个包。
        core::PK<core::Host> pk(pkx.pk(), pkx.plen() + core::PKG_HDR_LEN);
        if (s->send(pk) < 0) {
            xERROR("{} 发送失败", s->to_string());
        }
    }
}


int
typhon::kcp::Server::on_c2s(Session::Ptr s, core::PK<core::Host> &pk) noexcept {
    if (!s->authed()) {
        return xERR_NOT_AUTH;
    }

    auto sv = get_serv(pk->dst_id);
    if (sv == nullptr) {
        xERROR("{} 转包: invalid dst_id [{}]", s->to_string(), pk->dst_id);
        return xERR_PKT_DST;
    }

    if (!sv->is_connected() || !sv->authed()) {
        xWARN("后台服务 {} 还未鉴权完成", sv->id());
        return xOK;
    }

    core::PKx<core::Host> pkx(pk.raw() - core::PKX_HDR_LEN);
    pkx->len = (uint16_t)(core::PKX_HDR_LEN + pk.len());
    pkx->src_id = s->conv();
    pkx->src_addr = s->remote_addr_u32();

    if (sv->send(pkx, tnow_) < 0) {
        xERROR("{} 转发消息至 {} 失败: {}", s->to_string(), sv->id(), errno);
    }

    return xOK;
}