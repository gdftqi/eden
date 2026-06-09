#include "typhon.hpp"
#include "kcp/config.hpp"
#include <functional>


static constexpr int MAX_EVENTS  = 1;    // 只有一个 stop evfd
static constexpr int INTERVAL_MS = 10000;


void
typhon::Server::run() noexcept {
    auto stopped = core::State::Stopped;
    if (!state_.compare_exchange_strong(stopped, core::State::Starting)) {
        return;
    }

    int n = std::thread::hardware_concurrency();
    n = n > 1 ? n - 1 : 1;

    // 1. XDP envelope MAC 过滤先 attach.
    //    顺序敏感: 必须在 socket bind 之前生效, 否则启动期间被攻击会让垃圾流量
    //    直接进 kernel UDP stack. XDP 提前挂载等于"开机即受保护".
    if (!envelope_bpf_path_.empty()) {
        // 从 host_ ("0.0.0.0:5555") 解析端口给 XDP target_port 用.
        // ctor 已经 ASSERT 过 host_ 含 ':', 这里安全.
        auto colon = host_.find_last_of(':');
        int port = ::atoi(host_.c_str() + colon + 1);
        ASSERT(port > 0 && port <= 65535, "invalid port in host {}", host_);

        int rc = envelope_.init(envelope_bpf_path_.c_str(), (uint16_t)port, kcp::Conf::instance()->shkey());
        if (rc != 0) {
            xERROR("envelope filter init failed: rc = {}", rc);
            state_.store(core::State::Stopped);
            return;
        }

        rc = envelope_.attach(ifname_.c_str());
        if (rc != 0) {
            xERROR("envelope filter attach to {} failed: rc = {}", ifname_, rc);
            state_.store(core::State::Stopped);
            return;
        }
    }

    // 2. Router 加载 sk_reuseport BPF (kcp.bpf.o), 写 num_workers 到 .rodata.
    if (!kcp_bpf_path_.empty()) {
        int rc = router_.init(kcp_bpf_path_.c_str(), n);
        if (rc != 0) {
            xERROR("router init failed: rc = {}", rc);
            state_.store(core::State::Stopped);
            return;
        }
    }

    // 3. 创建 KcpServer
    for (int i = 0; i < n; ++i) {
        auto s = std::make_unique<kcp::Server>(host_.c_str(), serv_ev_);
        ASSERT(s->fd() != core::INVALID_SOCKET, "创建 kcp server 失败");

        if (!kcp_bpf_path_.empty()) {
            ASSERT(router_.register_socket(i, s->fd()) == 0, "注册 socket 失败");
        }

        ks_pool_.emplace_back(std::move(s));
    }

    // 4. 挂载 sk_reuseport 程序到 SO_REUSEPORT 组
    if (!kcp_bpf_path_.empty()) {
        ASSERT(router_.attach(ks_pool_[0]->fd()) == 0, "挂载 BPF 程序失败");
    }

    // 5. 启动所有 worker 线程
    for (auto& s : ks_pool_) {
        threads_.emplace_back(std::bind(&kcp::Server::run, s.get()));
    }

    init();
    ::epoll_event evs[MAX_EVENTS];
    state_.store(core::State::Running);
    
    while (running()) {
        n = ::epoll_wait(epfd_, evs, MAX_EVENTS, INTERVAL_MS);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            xERROR("epoll_wait failed: errno = {}, errstr = {}", errno, ::strerror(errno));
            break;
        }

        if (n > 0 && evs[0].data.fd == stop_evfd_) {
            break;
        }

        update_serv();
    }

    for (auto& s : ks_pool_) {
        s->stop();
    }

    for (auto& t : threads_) {
        t.join();
    }

    ks_pool_.clear();
    threads_.clear();

    release();
    state_.store(core::State::Stopped);
}


void
typhon::Server::init() noexcept {
    epfd_ = ::epoll_create1(0);
    ASSERT(epfd_ != core::INVALID_SOCKET, "epoll_create1 failed: errno = {}, errstr = {}", errno, ::strerror(errno));

    stop_evfd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT(stop_evfd_ != core::INVALID_SOCKET, "eventfd failed: errno = {}, errstr = {}", errno, ::strerror(errno));

    ::epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = stop_evfd_;
    ASSERT(::epoll_ctl(epfd_, EPOLL_CTL_ADD, stop_evfd_, &ev) == 0, "epoll_ctl failed: errno = {}, errstr = {}", errno, ::strerror(errno));
}


void
typhon::Server::release() noexcept {
    if (epfd_ != core::INVALID_SOCKET) {
        ::close(epfd_);
        epfd_ = core::INVALID_SOCKET;
    }

    if (stop_evfd_ != core::INVALID_SOCKET) {
        ::close(stop_evfd_);
        stop_evfd_ = core::INVALID_SOCKET;
    }
}


void
typhon::Server::update_serv() noexcept {
    // TODO: 改为ETCD 查询服务
    for (auto& s : ks_pool_) {
        auto* arg = (core::NewServArg*)::mi_malloc(sizeof(core::NewServArg));
        ::memset(arg, 0, sizeof(core::NewServArg));
        arg->id = 10000;
        ::strcpy(arg->host, "127.0.0.1:6688");
        s->notify(new core::QEvent(core::QEvent::Type::NewServ, arg));
    }
}