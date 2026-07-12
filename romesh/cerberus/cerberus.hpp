#ifndef __CERBERUS_HPP__
#define __CERBERUS_HPP__


#include "bpf/router.hpp"
#include "bpf/envelope_filter.hpp"
#include "kcp/server.hpp"
#include "tcp/server.hpp"
#include "utils/etcd.hpp"


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


    const YAML::Node&
    root() const noexcept {
        return root_;
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

        root["kcp"]["timeout"] = server_.timeout;
        root["kcp"]["id"] = server_.id;
        root_ = root;
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
    YAML::Node                root_;
}; // class Conf;


/**
 * @brief Typhon 服务
 *
 * 启动流程 (顺序敏感):
 *   1. EnvelopeFilter init + attach 网卡       — XDP MAC 校验先生效, 即使后续步骤
 *                                                 期间被攻击, 垃圾流量也进不了内核
 *   2. Router init (加载 sk_reuseport ELF)
 *   3. 创建 N 个 KcpServer (各自 udp_bind, SO_REUSEPORT)
 *   4. Router register_socket + attach        — 把 socket 注册进 sock_map + 挂载 BPF
 *   5. 启动 N 个 KcpServer worker 线程
 *
 * 析构顺序刚好相反, 析构链各自处理资源回收。
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
     * @param ev                KcpServer 事件接口 (业务回调)
     * @param host              监听 host:port, 例如 "0.0.0.0:5555"
     * @param ifname            attach XDP 的网卡名 (例如 "eth0" / "ens3" / "lo");
     *                          默认 "lo" 适合开发调试, 生产部署请传真实网卡
     * @param kcp_bpf_path      sk_reuseport BPF 程序 ELF 路径 (kcp.bpf.o);
     *                          空串表示不启用 BPF 路由 (落到 kernel 默认 hash, 不保证
     *                          同 conv 落同 worker, 见 PLAN.md)
     * @param envelope_bpf_path XDP envelope MAC 过滤 BPF 程序 ELF 路径 (envelope.bpf.o);
     *                          空串表示不启用 XDP 防御层 (开发 / 调试时可用)
     */
    explicit
    Cerberus(typhon::kcp::IEvent* ev, const char* host = "", const char* ifname = "", const char* kcp_bpf_path = "", const char* envelope_bpf_path = "") noexcept;


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
        if (::write(evwfd_, &serv_id, sizeof(serv_id)) != sizeof(serv_id)) {
            xERROR("write failed: errno = {}, errstr = {}", errno, ::strerror(errno));
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


    typhon::core::SOCKET                  epfd_              { typhon::core::INVALID_SOCKET }; // epoll fd
    typhon::core::SOCKET                  evrfd_             { typhon::core::INVALID_SOCKET }; // event read fd
    typhon::core::SOCKET                  evwfd_             { typhon::core::INVALID_SOCKET }; // event read fd
    uint64_t                              tnow_              { 0 };                            // 当前时间
    std::atomic<typhon::core::State>      state_             { typhon::core::State::Stopped }; // 状态
    typhon::kcp::IEvent*                  event_             { nullptr };                      // 服务事件
    std::string                           host_;                                               // 监听 host:port
    std::string                           ifname_;                                             // 网卡名 (XDP attach)
    std::string                           kcp_bpf_path_;                                       // kcp.bpf.o 路径
    std::string                           envelope_bpf_path_;                                  // envelope.bpf.o 路径
    typhon::bpf::EnvelopeFilter           envelope_;                                           // XDP MAC 过滤
    typhon::bpf::Router                   router_;                                             // SO_REUSEPORT 路由
    std::vector<typhon::kcp::Server::Ptr> ks_pool_;                                            // kcp server pool
    std::vector<std::thread>              threads_;                                            // 线程池
    ServSet                               servs_;                                              // 服务集合
}; // class Cerberus;


#endif // __CERBERUS_HPP__