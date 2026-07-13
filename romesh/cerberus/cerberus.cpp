#include "cerberus.hpp"
#include "utils/etcd.hpp"
#include "kcp/config.hpp"
#include <functional>


static constexpr int MAX_EVENTS  = 1;
static constexpr int INTERVAL_MS = 5000;


Cerberus::Cerberus(typhon::kcp::IEvent* ev) noexcept  
    : event_(ev) {
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
Cerberus::run() noexcept {
    auto stopped = typhon::core::State::Stopped;
    if (!state_.compare_exchange_strong(stopped, typhon::core::State::Starting)) {
        return;
    }

    int n = std::thread::hardware_concurrency();
    n = n > 1 ? n - 1 : 1;

    // 1. XDP envelope MAC 过滤先 attach.
    //    顺序敏感: 必须在 socket bind 之前生效, 否则启动期间被攻击会让垃圾流量
    //    直接进 kernel UDP stack. XDP 提前挂载等于"开机即受保护".
    if (!envelope_bpf_path_.empty()) {
        auto colon = host_.find_last_of(':');
        int port = ::atoi(host_.c_str() + colon + 1);
        ASSERT(port > 0 && port <= 65535, "invalid port in host {}", host_);

        int rc = envelope_.init(envelope_bpf_path_.c_str(), (uint16_t)port, typhon::kcp::Conf::instance()->siphash());
        if (rc != 0) {
            xERROR("envelope filter init failed: rc = {}", rc);
            state_.store(typhon::core::State::Stopped);
            return;
        }

        rc = envelope_.attach(ifname_.c_str());
        if (rc != 0) {
            xERROR("envelope filter attach to {} failed: rc = {}", ifname_, rc);
            state_.store(typhon::core::State::Stopped);
            return;
        }
    }

    // 2. Router 加载 sk_reuseport BPF (kcp.bpf.o), 写 num_workers 到 .rodata.
    if (!kcp_bpf_path_.empty()) {
        int rc = router_.init(kcp_bpf_path_.c_str(), n);
        if (rc != 0) {
            xERROR("router init failed: rc = {}", rc);
            state_.store(typhon::core::State::Stopped);
            return;
        }
    }
    
    // 3. 创建 KcpServer
    for (int i = 0; i < n; ++i) {
        auto s = std::make_unique<typhon::kcp::Server>(i, host_.c_str(), event_);
        ASSERT(s->fd() != typhon::core::INVALID_SOCKET, "创建 kcp server 失败");

        if (!kcp_bpf_path_.empty()) {
            ASSERT(router_.register_socket(i, s->fd()) == 0, "注册 socket 失败");
        }

        kcp_servs_.emplace_back(std::move(s));
    }

    // 4. 挂载 sk_reuseport 程序到 SO_REUSEPORT 组
    if (!kcp_bpf_path_.empty()) {
        ASSERT(router_.attach(kcp_servs_[0]->fd()) == 0, "挂载 BPF 程序失败");
    }

    // 5. 启动所有 worker 线程
    for (auto& s : kcp_servs_) {
        threads_.emplace_back(std::bind(&typhon::kcp::Server::run, s.get()));
    }

    init();
    event_->on_init(this);

    int i;
    ::epoll_event evs[MAX_EVENTS];
    state_.store(typhon::core::State::Running);

    while (running()) {
        n = ::epoll_wait(epfd_, evs, MAX_EVENTS, INTERVAL_MS);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            xERROR("epoll_wait failed: errno = {}, errstr = {}", errno, ::strerror(errno));
            break;
        }

        tnow_ = typhon::utils::systime_ms();
        for (i = 0; i < n; ++i) {
            on_event_handle(evs[i]);
        }

        update_serv();
    }

    for (auto& s : kcp_servs_) {
        s->stop();
    }

    for (auto& t : threads_) {
        t.join();
    }

    kcp_servs_.clear();
    threads_.clear();

    update_serv();
    release();
    state_.store(typhon::core::State::Stopped);
    event_->on_stopped(this);
}


void
Cerberus::init() noexcept {
    epfd_ = ::epoll_create1(0);
    ASSERT(epfd_ != typhon::core::INVALID_SOCKET, "epoll_create1 failed: errno = {}, errstr = {}", errno, ::strerror(errno));

    typhon::core::SOCKET fds[2];
    ASSERT(::pipe2(fds, O_NONBLOCK | O_CLOEXEC) == xOK, "pipe2 failed: errno = {}, errstr = {}", errno, ::strerror(errno));

    evrfd_ = fds[0];
    evwfd_ = fds[1];

    ::epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = evrfd_;
    ASSERT(::epoll_ctl(epfd_, EPOLL_CTL_ADD, evrfd_, &ev) == 0, "epoll_ctl failed: errno = {}, errstr = {}", errno, ::strerror(errno));
}


void
Cerberus::release() noexcept {
    if (epfd_ != typhon::core::INVALID_SOCKET) {
        ::close(epfd_);
        epfd_ = typhon::core::INVALID_SOCKET;
    }

    if (evrfd_ != typhon::core::INVALID_SOCKET) {
        ::close(evrfd_);
        evrfd_ = typhon::core::INVALID_SOCKET;
    }

    if (evwfd_ != typhon::core::INVALID_SOCKET) {
        ::close(evwfd_);
        evwfd_ = typhon::core::INVALID_SOCKET;
    }
}


void
Cerberus::update_serv() noexcept {
    constexpr uint64_t AUTH_INTERVAL = 120000;

    static const typhon::utils::EtcdConfig* etcd   = nullptr;
    static const typhon::core::ServerInfo*  server = nullptr;

    static bool        put_flag = false;
    static uint64_t    last_update = 0;
    static std::string lease;
    static std::string token;

    if (etcd == nullptr) {
        etcd = Conf::instance()->etcd();
    }

    if (server == nullptr) {
        server = Conf::instance()->server();
    }
    
    typhon::utils::EtcdRsp rsp;
    auto* url = etcd->url.c_str();

    if (token.empty() || tnow_ - last_update > AUTH_INTERVAL) {
        if (typhon::utils::etcd_auth(&rsp, url, etcd->user.c_str(), etcd->pass.c_str()) != 0) {
            token.clear();
            return;
        }

        token = rsp.token;
    }

    auto state = state_.load(std::memory_order_relaxed);
    if (state != typhon::core::State::Starting && state != typhon::core::State::Running) {
        typhon::utils::etcd_delete(&rsp, url, token.c_str(), server->key.c_str());
        token.clear();
        return;
    }

    if (tnow_ - last_update < INTERVAL_MS) {
        return;
    }

    if (!put_flag) {
        if (typhon::utils::etcd_grant(&rsp, url, etcd->ttl) != 0) {
            token.clear();
            return;
        }

        lease = rsp.id;
        auto k = server->key.c_str();
        auto v = server->val.c_str();

        if (typhon::utils::etcd_put(&rsp, url, token.c_str(), k, v, lease.c_str()) != 0) {
            token.clear();
            return;
        }

        put_flag = true;
        xINFO("{} 注册 etcd 成功", k);
    } else {
        if (typhon::utils::etcd_keepalive(&rsp, url, token.c_str(), lease.c_str()) != 0) {
            put_flag = false;
            token.clear();
            return;
        }
    }

    if (typhon::utils::etcd_get_prefix(&rsp, url, token.c_str(), "/public") != 0) {
        token.clear();
        return;
    }

    auto& kvs = rsp.kvs;
    for (auto& kv: kvs) {
        typhon::core::ServerInfo s;
        if (s.from_json(kv.second) != 0) {
            continue;
        }

        if (s.id == 0) {
            xWARN("{} 服务 ID {} 无效", kv.first, s.id);
            continue;
        }

        if (s.host.length() == 0 || s.host.length() > sizeof(typhon::core::AddServArg::host)) {
            xWARN("{} 服务 Host {} 无效", kv.first, s.host);
            continue;
        }

        if (servs_.count(s.id) > 0) {
            continue;
        }

        for (auto& ks: kcp_servs_) {
            auto* arg = new typhon::core::AddServArg;
            arg->id = s.id;
            ::strncpy(arg->host, s.host.c_str(), sizeof(arg->host) - 1);
            ks->notify(new typhon::core::QEvent(typhon::core::QEvent::Type::AddServ, arg));
        }

        servs_.insert(s.id);
    }

    last_update = tnow_;
}


void
Cerberus::on_event_handle(const ::epoll_event& ev) noexcept {
    if (ev.events & EPOLLIN) {
        uint8_t data[sizeof(Event) * 256];

        while (1) {
            ssize_t n = ::read(evrfd_, &data, sizeof(data));
            if (n <= 0) {
                if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                    xERROR("read eventfd failed: errno = {}, errstr = {}", errno, ::strerror(errno));
                }
                break;
            }

            for (uint8_t* p = data, *end = p + n; p < end; p += sizeof(Event)) {
                Event* ev = (Event*)p;

                switch (ev->type) {
                case EventType::OnServDisconnected:
                    on_serv_disconnected(ev);
                    break;

                case EventType::OnUserConnected:
                    on_user_connected(ev);
                    break;

                case EventType::OnUserDisconnected:
                    on_user_disconnected(ev);
                    break;

                case EventType::OnUserSend:
                    on_user_send(ev);
                    break;

                default:
                    xFATAL("无效的事件类型");
                    break;
                }
            }
        }
    } // if (ev.events & EPOLLIN);
}


void
Cerberus::on_serv_disconnected(const Event* ev) noexcept {
    auto itr = servs_.find(ev->u32_val);
    if (itr != servs_.end()) {
        servs_.erase(itr);
    }
}


void
Cerberus::on_user_connected(const Event*) noexcept {
    // TODO: redis 写入 用户路由表
}


void
Cerberus::on_user_disconnected(const Event*) noexcept {
    // TODO: redis 删除 用户路由表
}


void
Cerberus::on_user_send(const Event* ev) noexcept {
    auto* arg = new typhon::core::KcpSendArg;
    arg->raw = (uint8_t*)ev->ptr_val;
    arg->len = ev->u32_val;
    ASSERT(ev->i32_val < kcp_servs_.size(), "下标越界, 请检查 为 kcp server 分配的 idx");
    kcp_servs_[ev->i32_val]->notify(new typhon::core::QEvent(typhon::core::QEvent::Type::KcpSend, arg));
}