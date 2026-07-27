#include "kcp/server.hpp"
#include <functional>


static constexpr int MAX_EVENTS  = 1;
static constexpr int INTERVAL_MS = 5000;


adam::kcp::Server::Server(IHook* hook) noexcept
    : hook_(hook) {
    ASSERT(::sodium_init() == 0, "libsodium 初始化失败");

    if (host_.empty()) {
        host_ = Conf::instance()->server()->host;
    }

    if (ifname_.empty()) {
        ifname_ = Conf::instance()->ifname();
    }

    if (kcp_bpf_path_.empty()) {
        kcp_bpf_path_ = Conf::instance()->kcp_bpf_path();
    }

    if (envelope_bpf_path_.empty()) {
        envelope_bpf_path_ = Conf::instance()->envelope_bpf_path();
    }

    ASSERT(host_.find(':') != std::string::npos, "invalid host: {}", host_);
    // 这里只绑定 0.0.0.0 地址, 全地址只是用来写入 etcd
    host_ = host_.substr(host_.find(':'));
}


void
adam::kcp::Server::run() noexcept {
    auto expected = adam::core::State::Stopped;
    if (!state_.compare_exchange_strong(expected, adam::core::State::Starting)) {
        return;
    }

    // -------------------------------------- 启动步骤 --------------------------------------

    int n = (int)Conf::instance()->server()->nthreads;

    // 1. XDP envelope MAC 过滤先 attach.
    //    顺序敏感: 必须在 socket bind 之前生效, 否则启动期间被攻击会让垃圾流量
    //    直接进 kernel UDP stack. XDP 提前挂载等于"开机即受保护".
    if (!envelope_bpf_path_.empty()) {
        auto colon = host_.find_last_of(':');
        int port = ::atoi(host_.c_str() + colon + 1);
        ASSERT(port > 0 && port <= 65535, "invalid port in host {}", host_);

        int rc = envelope_.init(envelope_bpf_path_.c_str(), (uint16_t)port, adam::kcp::Conf::instance()->siphash());
        if (rc != 0) {
            xERROR("envelope filter init failed: rc = {}", rc);
            state_.store(adam::core::State::Stopped);
            return;
        }

        rc = envelope_.attach(ifname_.c_str());
        if (rc != 0) {
            xERROR("envelope filter attach to {} failed: rc = {}", ifname_, rc);
            state_.store(adam::core::State::Stopped);
            return;
        }
    }

    // 2. Router 加载 sk_reuseport BPF (kcp.bpf.o), 写 num_workers 到 .rodata.
    if (!kcp_bpf_path_.empty()) {
        int rc = router_.init(kcp_bpf_path_.c_str(), n);
        if (rc != 0) {
            xERROR("router init failed: rc = {}", rc);
            state_.store(adam::core::State::Stopped);
            return;
        }
    }
    
    // 3. 创建 KcpWorker
    for (int i = 0; i < n; ++i) {
        auto s = std::make_unique<adam::kcp::Worker>(this, i);
        ASSERT(s->fd() != INVALID_SOCKET, "创建 kcp worker 失败");

        if (!kcp_bpf_path_.empty()) {
            ASSERT(router_.register_socket(i, s->fd()) == 0, "注册 socket 失败");
        }

        workers_.emplace_back(std::move(s));
    }

    // 4. 挂载 sk_reuseport 程序到 SO_REUSEPORT 组
    if (!kcp_bpf_path_.empty()) {
        ASSERT(router_.attach(workers_[0]->fd()) == 0, "挂载 BPF 程序失败");
    }

    // 5. 启动所有 worker 线程
    for (auto& s : workers_) {
        threads_.emplace_back(std::bind(&adam::kcp::Worker::run, s.get()));
    }

    init();
    hook_->on_init(this);

    int i;
    ::epoll_event evs[MAX_EVENTS];
    state_.store(adam::core::State::Running);

    while (running()) {
        n = ::epoll_wait(epfd_, evs, MAX_EVENTS, INTERVAL_MS);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            xERROR("epoll_wait failed: errno = {}, errstr = {}", errno, ::strerror(errno));
            break;
        }

        tnow_ = adam::utils::systime_ms();
        for (i = 0; i < n; ++i) {
            on_event_handle(evs[i]);
        }

        update_serv();
    }

    // -------------------------------------- 停止步骤 --------------------------------------
    
    // Step 1, 从 ETCD 中注销
    auto* etcd   = Conf::instance()->etcd();
    auto* server = Conf::instance()->server();
    adam::utils::EtcdRsp rsp;
    auto* url = etcd->url.c_str();

    if (adam::utils::etcd_auth(&rsp, url, etcd->user.c_str(), etcd->pass.c_str()) == 0) {
        auto token = rsp.token;
        adam::utils::etcd_delete(&rsp, url, token.c_str(), server->key.c_str());;
    }

    // Step 2, 停止 KcpWorker
    for (auto& s : workers_) {
        s->stop();
    }

    for (auto& t : threads_) {
        t.join();
    }

    // Step 3, 清理资源
    workers_.clear();
    threads_.clear();

    evque_.clear([](Event* ev) { delete ev; });
    release();
    state_.store(adam::core::State::Stopped);
    hook_->on_stopped(this);
}


void
adam::kcp::Server::init() noexcept {
    epfd_ = ::epoll_create1(0);
    ASSERT(epfd_ != INVALID_SOCKET, "epoll_create1 failed: errno = {}, errstr = {}", errno, ::strerror(errno));

    evfd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT(evfd_ != INVALID_SOCKET, "创建 停止事件 fd 失败: errno = {}, errstr = {}", errno, ::strerror(errno));

    ::epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = evfd_;
    ASSERT(::epoll_ctl(epfd_, EPOLL_CTL_ADD, evfd_, &ev) == 0, "epoll_ctl failed: errno = {}, errstr = {}", errno, ::strerror(errno));
}


void
adam::kcp::Server::release() noexcept {
    if (epfd_ != INVALID_SOCKET) {
        ::close(epfd_);
        epfd_ = INVALID_SOCKET;
    }

    if (evfd_ != INVALID_SOCKET) {
        ::close(evfd_);
        evfd_ = INVALID_SOCKET;
    }
}


void
adam::kcp::Server::update_serv() noexcept {
    // 重新鉴权的时间间隔(120s)
    constexpr uint64_t AUTH_INTERVAL = 120000;

    const adam::utils::EtcdConfig* etcd   = Conf::instance()->etcd();
    const adam::core::ServerInfo*  server = Conf::instance()->server();

    bool        put_flag    = false;
    uint64_t    last_update = 0;
    std::string lease;
    std::string token;

    if (state_ != core::State::Running) {
        return;
    }
    
    adam::utils::EtcdRsp rsp;
    auto* url = etcd->url.c_str();

    if (token.empty() || tnow_ - last_update > AUTH_INTERVAL) {
        if (adam::utils::etcd_auth(&rsp, url, etcd->user.c_str(), etcd->pass.c_str()) != 0) {
            token.clear();
            return;
        }

        token = rsp.token;
    }

    if (tnow_ - last_update < INTERVAL_MS) {
        return;
    }

    if (!put_flag) {
        if (adam::utils::etcd_grant(&rsp, url, etcd->ttl) != 0) {
            token.clear();
            return;
        }

        lease = rsp.id;
        auto k = server->key.c_str();
        auto v = server->val.c_str();

        if (adam::utils::etcd_put(&rsp, url, token.c_str(), k, v, lease.c_str()) != 0) {
            token.clear();
            return;
        }

        put_flag = true;
    } else {
        if (adam::utils::etcd_keepalive(&rsp, url, token.c_str(), lease.c_str()) != 0) {
            put_flag = false;
            token.clear();
            return;
        }
    }

    if (adam::utils::etcd_get_prefix(&rsp, url, token.c_str(), "/public") != 0) {
        token.clear();
        return;
    }

    auto& kvs = rsp.kvs;
    for (auto& kv: kvs) {
        adam::core::ServerInfo s;
        if (s.from_json(kv.second) != 0) {
            continue;
        }

        if (s.id == 0) {
            xWARN("{} 服务 ID {} 无效", kv.first, s.id);
            continue;
        }

        if (s.host.length() == 0 || s.host.length() >= sizeof(EnsureBackendArg::host)) {
            xWARN("{} 服务 Host {} 无效", kv.first, s.host);
            continue;
        }

        // 服务自报家门(router=true 即终端路由服务), 随发现结果下发给各 worker;
        // 网关不认识任何具体服务实例(名字/id 都不关心)。
        for (auto& w: workers_) {
            w->notify(new Message(
                Message::Type::EnsureBackend,
                new EnsureBackendArg(s.id, s.host.c_str(), s.pids, s.router))
            );
        }
    }

    last_update = tnow_;
}


void
adam::kcp::Server::on_event_handle(const ::epoll_event& ev) noexcept {
    if (ev.events & EPOLLIN) {
        while (1) {
            uint64_t data = 0;
            ssize_t n = ::read(evfd_, &data, sizeof(data));
            if (n <= 0) {
                if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                    xERROR("read eventfd failed: errno = {}, errstr = {}", errno, ::strerror(errno));
                }
                break;
            }
        }
    } // if (ev.events & EPOLLIN);

    drain_qevent();
    evq_working_.store(false);
    drain_qevent();
}


void
adam::kcp::Server::drain_qevent() noexcept {
    constexpr int EVQUE_BATCH = 16;
    int i, n;
    Event* ss[EVQUE_BATCH];
    while ((n = evque_.try_dequeue_bulk(ss, EVQUE_BATCH)) > 0) {
        for (i = 0; i < n; ++i) {
            // 目前没有事件需要处理
            delete ss[i];
        }
    }
}
