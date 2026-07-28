#ifndef __ADAM_KCP_SERVER_HPP__
#define __ADAM_KCP_SERVER_HPP__


#include "bpf/router.hpp"
#include "bpf/envelope_filter.hpp"
#include "tcp/server.hpp"
#include "kcp/worker.hpp"
#include "utils/etcd.hpp"


namespace adam::kcp {


/**
 * Kcp server 钩子
 */
class IHook {
public:
    /**
     * @brief 服务初始化事件
     */
    virtual void
    on_init(Server*) noexcept {
    }


    /**
     * @brief 服务停止事件
     */
    virtual void
    on_stopped(Server*) noexcept {
    }


    /**
     * @brief 会话连接事件
     * @return 成功返回 0, 否则返回 -1
     */
    virtual int
    on_sess_connected(Session::Ptr) noexcept {
        return 0;
    }


    /**
     * @brief 会话断开事件
     */
    virtual void
    on_sess_disconnected(Session::Ptr) noexcept {
    }


    /**
     * @brief 后端服务注册成功事件
     */
    virtual void
    on_serv_registed(tcp::Connector::Ptr) noexcept {
    }


    /**
     * @brief 后端服务断开事件
     */
    virtual void
    on_serv_unregisted(tcp::Connector::Ptr) noexcept {
    }


    virtual void
    on_terminal_binded(Session::Ptr, uint32_t) noexcept {
    }


    virtual void
    on_terminal_unbinded(Session::Ptr, uint32_t) noexcept {
    }
}; // class IEvent;


/**
 * @brief kcp server
 */
class Server {
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    Server(Server&&) = delete;
    Server& operator=(Server&&) = delete;


    enum EventType {
        Stop,
    }; // enum SigType;


    struct Event {
        EventType type;
        uint32_t  u32;
        uint8_t*  ptr;


        explicit
        Event(EventType type) noexcept
            : type(type)
            , u32(0)
            , ptr(nullptr)
        {}


        Event(EventType type, uint32_t arg) noexcept
            : type(type)
            , u32(arg)
            , ptr(nullptr)
        {}


        Event(EventType type, uint8_t* arg) noexcept
            : type(type)
            , u32(0)
            , ptr(arg)
        {}


        ~Event() noexcept {
            if (ptr != nullptr) {
                ::mi_free(ptr);
            }
        }
    }; // struct Signal;


public:
    typedef std::unique_ptr<Server>       Ptr;
    typedef absl::flat_hash_set<uint32_t> ServSet;
    typedef std::vector<std::thread>      ThreadPool;
    typedef std::vector<Worker::Ptr>      WorkerPool;
    typedef utils::MPSC<Event*>           EvQue;

    typedef int (*PackageHandler)(Session::Ptr, core::Package*) noexcept;
    typedef absl::flat_hash_map<uint16_t, PackageHandler> PackageHandlers;


    /**
     * @brief 构造函数
     *
     * @param hook KcpServer 事件接口
     */
    explicit
    Server(IHook* hook) noexcept;


    /**
     * @brief 事件
     */
    IHook*
    hook() noexcept {
        return hook_;
    }


    /**
     * @brief 绑定的地址端口
     */
    const std::string&
    host() const noexcept {
        return host_;
    }


    /**
     * @brief kcp workers
     */
    WorkerPool*
    workers() noexcept {
        return &workers_;
    }


    /**
     * @brief 是否运行中
     */
    bool
    running() const noexcept {
        return state_.load(std::memory_order_relaxed) == adam::core::State::Running;
    }


    /**
     * @brief 注册消息句柄
     */
    void
    regist_handler(uint16_t pid, PackageHandler handler) noexcept {
        ASSERT(pid >= PID_CUSTOM, "PID: {} 无效", pid);
        ASSERT(state_.load() == core::State::Stopped, "服务启动之后不允许再次注册 PID句柄");

        if (handlers_.count(pid) > 0) {
            xWARN("ID 为 {} 消息句柄已存在", pid);
        }
        handlers_[pid] = handler;
    }


    /**
     * @brief 获取消息句柄
     */
    PackageHandler
    get_handler(uint16_t pid) noexcept {
        auto itr = handlers_.find(pid);
        return itr == handlers_.end() ? nullptr : itr->second;
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
        auto expected = adam::core::State::Running;
        if (state_.compare_exchange_strong(expected, adam::core::State::Stopping)) {
            notify(EventType::Stop);
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


    /**
     * @brief 事件句柄
     */
    void
    on_event_handle(const ::epoll_event& ev) noexcept;


    void
    notify(EventType type, uint32_t arg) noexcept {
        ASSERT(evque_.enqueue(new Event(type, arg)), "队列已满, 需要扩容");
        bool expected = false;
        if (evq_working_.compare_exchange_strong(expected, true)) {
            constexpr uint64_t v = 1;
            if (::write(evfd_, &v, sizeof(v)) != sizeof(v)) {
                xERROR("write failed: {}, {}", errno, ::strerror(errno));
            }
        }
    }


    void
    notify(EventType type, uint8_t* arg) noexcept {
        ASSERT(evque_.enqueue(new Event(type, arg)), "队列已满, 需要扩容");
        bool expected = false;
        if (evq_working_.compare_exchange_strong(expected, true)) {
            constexpr uint64_t v = 1;
            if (::write(evfd_, &v, sizeof(v)) != sizeof(v)) {
                xERROR("write failed: {}, {}", errno, ::strerror(errno));
            }
        }
    }


    void
    notify(EventType type) noexcept {
        ASSERT(evque_.enqueue(new Event(type)), "队列已满, 需要扩容");
        bool expected = false;
        if (evq_working_.compare_exchange_strong(expected, true)) {
            constexpr uint64_t v = 1;
            if (::write(evfd_, &v, sizeof(v)) != sizeof(v)) {
                xERROR("write failed: {}, {}", errno, ::strerror(errno));
            }
        }
    }


    void
    drain_qevent() noexcept;


    // --------------------------------------------------------------------
    // 基础属性
    // --------------------------------------------------------------------

    SOCKET                   epfd_              { INVALID_SOCKET };               // epoll fd
    uint64_t                 tnow_              { 0 };                            // 当前时间
    std::atomic<core::State> state_             { adam::core::State::Stopped };   // 状态
    IHook*                   hook_              { nullptr };                      // 服务事件
    std::string              host_;                                               // 直实绑定的地址端口
    std::string              ifname_;                                             // 网卡名 (XDP attach)
    std::string              kcp_bpf_path_;                                       // kcp.bpf.o 路径
    std::string              envelope_bpf_path_;                                  // envelope.bpf.o 路径
    bpf::EnvelopeFilter      envelope_;                                           // XDP MAC 过滤
    bpf::Router              router_;                                             // SO_REUSEPORT 路由
    ThreadPool               threads_;                                            // 线程池
    WorkerPool               workers_;                                            // kcp workers
    PackageHandlers          handlers_;                                           // 服务句柄

    // --------------------------------------------------------------------
    // 事件属性
    // --------------------------------------------------------------------
    
    SOCKET           evfd_        { INVALID_SOCKET }; // event fd
    std::atomic_bool evq_working_ { false };          // 事件队列状态
    EvQue            evque_;                          // 事件队列
}; // class Server;


} // namespace adam::kcp


#endif // __ADAM_KCP_SERVER_HPP__
