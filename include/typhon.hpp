#ifndef __TYPHON_HPP__
#define __TYPHON_HPP__


#include "bpf/router.hpp"
#include "bpf/envelope_filter.hpp"
#include "kcp/server.hpp"
#include "tcp/server.hpp"


namespace typhon {


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
class Server {
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    Server(Server&&) = delete;
    Server& operator=(Server&&) = delete;


public:
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
    explicit Server(kcp::Server::IEvent* ev,
                    const char* host,
                    const char* ifname = "lo",
                    const char* kcp_bpf_path = "",
                    const char* envelope_bpf_path = "") noexcept
        : serv_ev_(ev)
        , host_(host)
        , ifname_(ifname ? ifname : "lo")
        , kcp_bpf_path_(kcp_bpf_path ? kcp_bpf_path : "")
        , envelope_bpf_path_(envelope_bpf_path ? envelope_bpf_path : "") {
        ASSERT(host_.find(':') != std::string::npos, "invalid host: {}", host_);
    }


    bool
    running() const noexcept {
        return state_.load(std::memory_order_relaxed) == core::State::Running;
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
        auto running = core::State::Running;
        if (state_.compare_exchange_strong(running, core::State::Stopping)) {
            static constexpr uint64_t event = 1;
            if (::write(stop_evfd_, &event, sizeof(event)) != sizeof(event)) {
                xERROR("write to stop_evfd_ failed: errno = {}, errstr = {}", errno, ::strerror(errno));
            }
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


    core::SOCKET                  epfd_              { core::INVALID_SOCKET }; // epoll fd
    core::SOCKET                  stop_evfd_         { core::INVALID_SOCKET }; // stop event fd
    kcp::Server::IEvent*          serv_ev_           { nullptr };              // 服务事件
    std::atomic<core::State>      state_             { core::State::Stopped }; // 状态
    std::string                   host_;                                       // 监听 host:port
    std::string                   ifname_;                                     // 网卡名 (XDP attach)
    std::string                   kcp_bpf_path_;                               // kcp.bpf.o 路径
    std::string                   envelope_bpf_path_;                          // envelope.bpf.o 路径
    bpf::EnvelopeFilter           envelope_;                                   // XDP MAC 过滤
    bpf::Router                   router_;                                     // SO_REUSEPORT 路由
    std::vector<kcp::Server::Ptr> ks_pool_;                                    // kcp server pool
    std::vector<std::thread>      threads_;                                    // 线程池
}; // class Server;


} // namespace typhon;


#endif // __TYPHON_HPP__