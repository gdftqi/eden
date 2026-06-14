#ifndef __TYPHON_TCP_SERVER_HPP__
#define __TYPHON_TCP_SERVER_HPP__


#include "tcp/proc.hpp"


namespace typhon::tcp {


/**
 * @brief TcpServer TCP 服务端
 */
class Server {
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    Server(Server&&) = delete;
    Server& operator=(Server&&) = delete;


public:
    /**
     * @brief TCP 服务事件回调接口。
     *
     * @warning **`on_connected` / `on_disconnected` 会被多个 worker 线程并发触发**。
     *          tcp::Server 主线程只负责 accept,session 真正的 add / remove 在
     *          worker 线程里(`on_qe_add_sess_handle` / `on_qe_rmv_sess_handle`),
     *          按 `fd % workers_.size()` 分流到 N 个 worker。也就是说:
     *            - on_init / on_stopped:server 主线程串行调,只 1 次
     *            - on_connected / on_disconnected:不同 fd 可能在不同 worker
     *              线程同时调到
     *
     *          实现方必须**自己保证线程安全**:
     *            - 写共享容器(玩家表 / 在线名单等)要加锁 / shard by fd 等
     *            - 读全局只读状态无需保护
     *            - 调 spdlog 等 thread-safe 库直接用即可
     *
     *          同一个 session(同一 fd)上的回调不会重入,因为同一 fd 始终被
     *          同一个 worker 处理(`fd % workers_.size()`)。
     */
    class IEvent {
    public:
        virtual
        ~IEvent() noexcept
        {}


        virtual void
        on_init(Server*) noexcept
        {}


        virtual void
        on_stopped(Server*) noexcept
        {}


        virtual int
        on_connected(Session::Ptr) noexcept {
            return 0;
        }


        virtual void
        on_disconnected(Session::Ptr) noexcept
        {}
    }; // class IEvent;


    static constexpr int MAX_CONN = 2048; ///< 最大连接数


    typedef void (*PackageHandler)(Session::Ptr, core::PKx<core::Host>&) noexcept;


    explicit
    Server(const char* host, IEvent* event) noexcept
        : event_(event)
        , host_(host) {
        ASSERT(event_ != nullptr, "event handler cannot be null");
        ASSERT(::sodium_init() == 0, "libsodium 初始化失败");
    }


    ~Server() noexcept {
        release();
    }


    std::string
    host() const noexcept {
        return host_;
    }


    const Session::Ptr*
    sessions() const noexcept {
        return gws_;
    }


    int
    worker_size() const noexcept {
        return (int)procs_.size();
    }


    bool
    running() const noexcept {
        return state_.load(std::memory_order_relaxed) == core::State::Running;
    }


    void
    run() noexcept;


    void
    stop() noexcept {
        if (running()) {
            core::State expected = core::State::Running;
            if (state_.compare_exchange_strong(expected, core::State::Stopping)) {
                constexpr uint64_t event = 1;
                if (::write(stop_evfd_, &event, sizeof(event)) != sizeof(event)) {
                    xWARN("write failed: errno = {}, errstr = {}", errno, ::strerror(errno));
                }
            }
        }
    }


    Session::Ptr
    get_session(core::SOCKET fd) noexcept {
        ASSERT(fd >= 0 && fd < MAX_CONN, "invalid fd: {}", fd);
        return gws_[fd];
    }


    void
    add_session(core::SOCKET fd, Proc* w) noexcept {
        ASSERT(fd >= 0 && fd < MAX_CONN, "invalid fd: {}", fd);
        gws_[fd] = Session::create(fd, w);
        if (event_->on_connected(gws_[fd]) != 0) {
            ASSERT(::epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, nullptr) == 0, "failed to remove session from epoll: errno = {}, errstr = {}", errno, ::strerror(errno));
            gws_[fd] = nullptr;
        }
    }


    void
    remove_session(core::SOCKET fd, bool del_from_epoll = true) noexcept {
        ASSERT(fd >= 0 && fd < MAX_CONN, "invalid fd: {}", fd);
        if (gws_[fd]) {
            if (del_from_epoll) {
                ASSERT(::epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, nullptr) == 0, "failed to remove session from epoll: errno = {}, errstr = {}", errno, ::strerror(errno));
            }
            event_->on_disconnected(gws_[fd]);
            gws_[fd] = nullptr;
        }
    }


    PackageHandler
    get_handler(uint16_t pkid) const noexcept {
        return pkid >= core::MAX_HANDLERS ? nullptr : handlers[pkid];
    }


    /**
     * @brief 注册 pk_id → handler 路由表项。
     *
     * @warning **必须在 run() 之前调用**。handlers[] 是裸数组,运行中由 worker 线程
     *          只读访问,本函数是唯一写点;没有任何同步原语保护。
     *          run() 起来之后再 regist_handler 属于跨线程裸读 + 裸写,UB。
     *
     * @param pkid    业务消息号
     * @param handler 处理函数指针;同一 pkid 重复注册会覆盖并打 WARN
     */
    void
    regist_handler(uint16_t pkid, PackageHandler handler) noexcept {
        ASSERT(pkid < core::MAX_HANDLERS, "pkid {} out of range, max = {}", pkid, core::MAX_HANDLERS);
        if (handlers[pkid] != nullptr) {
            xWARN("handler for pk_id {} already exists, will be overwritten", pkid);
        }

        handlers[pkid] = handler;
    }


private:
    void
    init() noexcept;


    void
    release() noexcept;


    void
    on_stop_handle(const ::epoll_event& ev) noexcept;


    void
    on_listen_handle(const ::epoll_event& ev) noexcept;


    void
    on_session_handle(const ::epoll_event& ev) noexcept;


    core::SOCKET             lfd_                         { core::INVALID_SOCKET };
    core::SOCKET             stop_evfd_                   { core::INVALID_SOCKET };
    core::SOCKET             epfd_                        { core::INVALID_SOCKET };
    IEvent*                  event_                       { nullptr };
    std::atomic<core::State> state_                       { core::State::Stopped };
    std::string              host_;   
    std::vector<Proc::Ptr>   procs_;   
    std::vector<std::thread> threads_;
    Session::Ptr             gws_[MAX_CONN]               { nullptr };
    PackageHandler           handlers[core::MAX_HANDLERS] { nullptr };
};

    
} // namespace typhon::tcp


#endif // __TYPHON_TCP_SERVER_HPP__