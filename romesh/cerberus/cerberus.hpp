#ifndef __CERBERUS_HPP__
#define __CERBERUS_HPP__


#include "bpf/router.hpp"
#include "bpf/envelope_filter.hpp"
#include "kcp/server.hpp"
#include "tcp/server.hpp"
#include "utils/etcd.hpp"


enum class EventType: uint32_t {
    None,
    OnServDisconnected,
    OnUserConnected,
    OnUserDisconnected,
};


#pragma pack(push, 4)

/**
 * @brief Cerberus 内部事件
 */
struct Event {
    EventType type    { EventType::None };
    uint32_t  u32_val { 0 };
}; // Event;

static_assert(sizeof(Event) == 8, "Event 对齐错误");

#pragma pack(pop)


class Conf {
    Conf(const Conf&) = delete;
    Conf& operator=(const Conf&) = delete;
    Conf(Conf&&) = delete;
    Conf& operator=(Conf&&) = delete;


public:
    static Conf*
    instance() noexcept {
        static Conf m;
        return &m;
    }


    const typhon::core::ServerInfo*
    server() const noexcept {
        return &server_;
    }


    const typhon::utils::EtcdConfig*
    etcd() const noexcept {
        return &etcd_;
    }


    std::string
    ifname() const noexcept {
        return ifname_;
    }


    std::string
    kcp_bpf_path() const noexcept {
        return kcp_bpf_path_;
    }


    std::string
    envelope_bpf_path() const noexcept {
        return envelope_bpf_path_;
    }


    void
    load(const char* cfname) {
        auto root = YAML::LoadFile(cfname);
        if (!root["server"] || server_.from_yaml(root["server"]) < 0) {
            xFATAL("config.server is invalid");
        }

        if (!root["etcd"] || etcd_.from_yaml(root["etcd"]) < 0) {
            xFATAL("config.etcd is invalid");
        }

        if (root["ifname"]) {
            ifname_ = root["ifname"].as<std::string>();
        }

        if (root["kcp_bpf_path"]) {
            kcp_bpf_path_ = root["kcp_bpf_path"].as<std::string>();
        }

        if (root["envelope_bpf_path"]) {
            envelope_bpf_path_ = root["envelope_bpf_path"].as<std::string>();
        }

        if (!root["kcp"]) {
            xFATAL("config.kcp is invalid");
        }

        auto k = root["kcp"];
        typhon::kcp::Conf::instance()->set_id(server_.id);
        typhon::kcp::Conf::instance()->set_timeout(server_.timeout);
        
        if (k["sndbuf"]) {
            typhon::kcp::Conf::instance()->set_sndbuf(k["sndbuf"].as<int>());
        }

        if (k["rcvbuf"]) {
            typhon::kcp::Conf::instance()->set_rcvbuf(k["rcvbuf"].as<int>());
        }
        
        if (k["sndwnd"]) {
            typhon::kcp::Conf::instance()->set_sndwnd(k["sndwnd"].as<int>());
        }

        if (k["rcvwnd"]) {
            typhon::kcp::Conf::instance()->set_rcvwnd(k["rcvwnd"].as<int>());
        }

        if (k["nodelay"]) {
            typhon::kcp::Conf::instance()->set_nodelay(k["nodelay"].as<int>());
        }

        if (k["interval"]) {
            typhon::kcp::Conf::instance()->set_interval(k["interval"].as<int>());
        }

        if (k["resend"]) {
            typhon::kcp::Conf::instance()->set_resend(k["resend"].as<int>());
        }

        if (k["nc"]) {
            typhon::kcp::Conf::instance()->set_nc(k["nc"].as<int>());
        }

        if (k["siphash"]) {
            typhon::kcp::Conf::instance()->set_siphash(k["siphash"].as<std::string>());
        }

        if (k["x25519_pk"]) {
            typhon::kcp::Conf::instance()->set_x25519_pk(k["x25519_pk"].as<std::string>());
        }

        if (k["x25519_sk"]) {
            typhon::kcp::Conf::instance()->set_x25519_sk(k["x25519_sk"].as<std::string>());
        }

        if (k["ed25519_pk"]) {
            typhon::kcp::Conf::instance()->set_ed25519_pk(k["ed25519_pk"].as<std::string>());
        }
    }


private:
    explicit
    Conf() noexcept
    {}


    typhon::core::ServerInfo  server_;
    typhon::utils::EtcdConfig etcd_;
    std::string               ifname_;
    std::string               kcp_bpf_path_;
    std::string               envelope_bpf_path_;
}; // class Conf;


/**
 * @brief cerberus 服务
 *
 * 启动流程 (顺序敏感):
 *   1. EnvelopeFilter init + attach 网卡       — XDP MAC 校验先生效, 即使后续步骤
 *                                                 期间被攻击, 垃圾流量也进不了内核
 *   2. Router init (加载 sk_reuseport ELF)
 *   3. 创建 N 个 KcpServer (各自 udp_bind, SO_REUSEPORT)
 *   4. Router register_socket + attach        — 把 socket 注册进 sock_map + 挂载 BPF
 *   5. 启动 N 个 KcpServer worker 线程
 *
 * 析构顺序刚好相反, 析构链各自处理资源回收
 */
class Cerberus {
    Cerberus(const Cerberus&) = delete;
    Cerberus& operator=(const Cerberus&) = delete;
    Cerberus(Cerberus&&) = delete;
    Cerberus& operator=(Cerberus&&) = delete;


public:
    typedef absl::flat_hash_set<uint32_t> ServSet;


    /**
     * @brief 构造函数
     *
     * @param ev KcpServer 事件接口 (业务回调)
     */
    explicit
    Cerberus(typhon::kcp::IEvent* ev) noexcept;


    bool
    running() const noexcept {
        return state_.load(std::memory_order_relaxed) == typhon::core::State::Running;
    }


    /**
     * @brief 启动服务
     */
    void
    run() noexcept;


    /**
     * @brief 停止服务
     */
    void
    stop() noexcept {
        auto running = typhon::core::State::Running;
        if (state_.compare_exchange_strong(running, typhon::core::State::Stopping)) {
            notify_serv_disconnected(0);
        }
    }


    void
    notify_serv_disconnected(uint32_t serv_id) noexcept {
        Event ev;
        ev.type = EventType::OnServDisconnected;
        ev.u32_val = serv_id;

        if (::write(evwfd_, &ev, sizeof(Event)) != sizeof(Event)) {
            xERROR("notify_serv_disconnected failed: errno = {}, errstr = {}", errno, ::strerror(errno));
        }
    }


    void
    notify_user_connected(uint32_t user_id) noexcept {
        Event ev;
        ev.type = EventType::OnUserConnected;
        ev.u32_val = user_id;

        if (::write(evwfd_, &ev, sizeof(Event)) != sizeof(Event)) {
            xERROR("notify_user_connected failed: errno = {}, errstr = {}", errno, ::strerror(errno));
        }
    }


    void
    notify_user_disconnected(uint32_t user_id) noexcept {
        Event ev;
        ev.type = EventType::OnUserDisconnected;
        ev.u32_val = user_id;

        if (::write(evwfd_, &ev, sizeof(Event)) != sizeof(Event)) {
            xERROR("notify_user_disconnected failed: errno = {}, errstr = {}", errno, ::strerror(errno));
        }
    }


private:
    void
    init() noexcept;


    void
    release() noexcept;


    /**
     * @brief 定时更新后台服务状态, 每次 epoll_wait 超时后调用
     */
    void
    update_serv() noexcept;


    void
    on_event_handle(const ::epoll_event& ev) noexcept;


    void
    on_serv_disconnected(const Event* ev) noexcept;


    void
    on_user_connected(const Event* ev) noexcept;


    void
    on_user_disconnected(const Event* ev) noexcept;


    typhon::core::SOCKET             epfd_              { typhon::core::INVALID_SOCKET }; // epoll fd
    typhon::core::SOCKET             evrfd_             { typhon::core::INVALID_SOCKET }; // event read fd
    typhon::core::SOCKET             evwfd_             { typhon::core::INVALID_SOCKET }; // event read fd
    uint64_t                         tnow_              { 0 };                            // 当前时间
    std::atomic<typhon::core::State> state_             { typhon::core::State::Stopped }; // 状态
    typhon::kcp::IEvent*             event_             { nullptr };                      // 服务事件
    std::string                      host_;                                               // 监听 host:port
    std::string                      ifname_;                                             // 网卡名 (XDP attach)
    std::string                      kcp_bpf_path_;                                       // kcp.bpf.o 路径
    std::string                      envelope_bpf_path_;                                  // envelope.bpf.o 路径
    typhon::bpf::EnvelopeFilter      envelope_;                                           // XDP MAC 过滤
    typhon::bpf::Router              router_;                                             // SO_REUSEPORT 路由
    std::vector<std::thread>         threads_;                                            // 线程池
    ServSet                          servs_;                                              // 服务集合
}; // class Cerberus;


#endif // __CERBERUS_HPP__