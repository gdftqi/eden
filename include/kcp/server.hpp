#ifndef __TYPHON_KCP_SERVER_HPP__
#define __TYPHON_KCP_SERVER_HPP__


#include "bpf/router.hpp"
#include "bpf/envelope_filter.hpp"
#include "kcp/event.hpp"
#include "tcp/server.hpp"
#include "kcp/worker.hpp"
#include "utils/etcd.hpp"


namespace typhon::kcp {


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
class Server {
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    Server(Server&&) = delete;
    Server& operator=(Server&&) = delete;


public:
    typedef absl::flat_hash_set<uint32_t> ServSet;
    typedef std::vector<std::thread>      ThreadPool;
    typedef std::vector<Worker::Ptr>      WorkerPool;


    /**
     * @brief 构造函数
     *
     * @param ev KcpServer 事件接口 (业务回调)
     */
    explicit
    Server(IEvent* ev) noexcept;


    IEvent*
    event() noexcept {
        return event_;
    }


    WorkerPool*
    workers() noexcept {
        return &workers_;
    }


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


    core::SOCKET             epfd_              { core::INVALID_SOCKET }; // epoll fd
    core::SOCKET             evrfd_             { core::INVALID_SOCKET }; // event read fd
    core::SOCKET             evwfd_             { core::INVALID_SOCKET }; // event read fd
    uint64_t                 tnow_              { 0 };                            // 当前时间
    std::atomic<core::State> state_             { typhon::core::State::Stopped }; // 状态
    IEvent*                  event_             { nullptr };                      // 服务事件
    std::string              host_;
    std::string              ifname_;                                             // 网卡名 (XDP attach)
    std::string              kcp_bpf_path_;                                       // kcp.bpf.o 路径
    std::string              envelope_bpf_path_;                                  // envelope.bpf.o 路径
    bpf::EnvelopeFilter      envelope_;                                           // XDP MAC 过滤
    bpf::Router              router_;                                             // SO_REUSEPORT 路由
    ThreadPool               threads_;                                            // 线程池
    WorkerPool               workers_;
    ServSet                  servs_;                                              // 服务集合
}; // class Server;


} // namespace typhon::kcp


#endif // __TYPHON_KCP_SERVER_HPP__