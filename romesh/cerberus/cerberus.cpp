#include "cerberus.hpp"
#include "utils/etcd.hpp"
#include "kcp/config.hpp"
#include <functional>


static constexpr int MAX_EVENTS  = 1;
static constexpr int INTERVAL_MS = 10000;


cerberus::Server::Server(typhon::kcp::IEvent* ev, const char* host, const char* ifname, const char* kcp_bpf_path, const char* envelope_bpf_path) noexcept  
    : event_(ev)
    , host_(host)
    , ifname_(ifname ? ifname : "lo")
    , kcp_bpf_path_(kcp_bpf_path ? kcp_bpf_path : "")
    , envelope_bpf_path_(envelope_bpf_path ? envelope_bpf_path : "") {

    ASSERT(::sodium_init() == 0, "libsodium 初始化失败");

    if (host_.empty()) {
        host_ = Conf::instance()->host();
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

    typhon::kcp::Conf::instance()->load(Conf::instance()->root()["kcp"]);

    ASSERT(host_.find(':') != std::string::npos, "invalid host: {}", host_);
}


void
cerberus::Server::run() noexcept {
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
        auto s = std::make_unique<typhon::kcp::Server>(host_.c_str(), event_);
        ASSERT(s->fd() != typhon::core::INVALID_SOCKET, "创建 kcp server 失败");

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

        if (n == 0) {
            update_serv();
            continue;
        }

        for (i = 0; i < n; ++i) {
            on_event_handle(evs[i]);
        }
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
    state_.store(typhon::core::State::Stopped);
    event_->on_stopped(this);
}


void
cerberus::Server::init() noexcept {
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
cerberus::Server::release() noexcept {
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
cerberus::Server::update_serv() noexcept {
    // TODO: 
    typhon::utils::EtcdRsp rsp;
    typhon::utils::etcd_auth(&rsp, "http://13.212.159.179:2379", "root", "123456");

    const uint32_t serv_id     = 10000;
    const char     serv_host[] = "172.31.6.248:6688";
    // const char     serv_host[] = "127.0.0.1:6688";

    if (servs_.count(serv_id)) {
        return;    
    }

    for (auto& s : ks_pool_) {
        auto* arg = (typhon::core::AddServArg*)::mi_malloc(sizeof(typhon::core::AddServArg));
        ::memset(arg, 0, sizeof(typhon::core::AddServArg));
        arg->id = serv_id;
        ::strcpy(arg->host, serv_host);
        s->notify(new typhon::core::QEvent(typhon::core::QEvent::Type::AddServ, arg));
    }

    servs_.insert(serv_id);
}


void
cerberus::Server::on_event_handle(const ::epoll_event& ev) noexcept {
    if (ev.events & EPOLLIN) {
        uint8_t data[1400];
        static_assert(sizeof(data) % sizeof(uint32_t) == 0);

        while (1) {
            auto n = ::read(evrfd_, &data, sizeof(data));
            if (n <= 0) {
                if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                    xERROR("read eventfd failed: errno = {}, errstr = {}", errno, ::strerror(errno));
                }
                break;
            }

            for (uint8_t* p = data, *end = p + n; p < end; p += sizeof(uint32_t)) {
                uint32_t serv_id = *(uint32_t*)p;
                if (serv_id == 0) {
                    // 服务停止
                    continue;
                }

                auto itr = servs_.find(serv_id);
                if (itr != servs_.end()) {
                    servs_.erase(itr);
                    xWARN("服务 {} 掉线", serv_id);
                }
            }
        }
    } // if (ev.events & EPOLLIN);
}