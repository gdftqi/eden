#ifndef __TYPHON_KCP_SERVER_HPP__
#define __TYPHON_KCP_SERVER_HPP__


#include "core/buffer.hpp"
#include "core/package.hpp"
#include "core/qevent.hpp"
#include "kcp/session.hpp"
#include "tcp/connector.hpp"
#include "utils/obj_pool.hpp"
#include "utils/spsc.hpp"


namespace typhon::kcp {


constexpr int MAX_RECV = 128;
constexpr int MAX_SEND = MAX_RECV;


/**
 * @brief KCP 协议服务器
 * @note 线程不安全, 必须在单线程中使用
 */
class Server {
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    Server(Server&&) = delete;
    Server& operator=(Server&&) = delete;


public:
    typedef utils::SPSC<core::QEvent*>                        EvQue;
    typedef std::unique_ptr<Server>                           Ptr;
    typedef utils::ObjPool<core::SndBuf>                      SndBufPool;
    typedef std::unordered_map<uint32_t, Session::Ptr>        SessionMap;
    typedef std::unordered_map<uint32_t, tcp::Connector::Ptr> ServMap;

    
    /**
     * @brief KCP 服务事件回调接口。
     *
     * @warning **所有回调可能被多个线程并发触发** —— `typhon::Server` 会构造
     *          N 个 `kcp::Server`(每个对应一个 worker 线程,SO_REUSEPORT 分流),
     *          这 N 个 Server 实例**共享同一个 IEvent**。也就是说:
     *            - on_init / on_stopped:每个 Server 启停一次 → N 次,跨 N 个线程
     *            - on_connected / on_disconnected / on_data:在各自 Server 的
     *              线程里触发,**同一时刻不同 conv 可能在不同线程同时调到**
     *
     *          实现方必须**自己保证线程安全**:
     *            - 写共享容器要加锁 / 用并发数据结构 / shard by conv 等
     *            - 读全局只读状态(配置 / 静态表)无需保护
     *            - 调 spdlog 等 thread-safe 库直接用即可
     *
     *          单 `kcp::Server` 实例内部是单线程,因此**同一 session 上的回调
     *          不会重入**(on_connected → on_data → on_disconnected 串行)。
     *          跨 session / 跨 Server 实例的并发要业务自处理。
     */
    class IEvent {
    public:
        virtual
        ~IEvent() noexcept
        {}


        /**
         * @brief 服务器初始化回调, 在 Server::run() 里 bind() 和 listen() 成功后调用
         */
        virtual void
        on_init(Server*) noexcept
        {}


        /**
         * @brief 服务器停止回调, 在 Server::run() 退出前调用
         */
        virtual void 
        on_stopped(Server*) noexcept 
        {}


        /**
         * @brief 会话连接回调,在 Server::add_session() 成功后调用
         * @return 返回非 0 表示拒绝连接, Server 会立刻 remove_session()
         */
        virtual int 
        on_connected(Session::Ptr) noexcept {
            return 0;
        }


        /**
         * @brief 会话断开回调,在 Server::remove_session() 里调用
         */
        virtual void 
        on_disconnected(Session::Ptr) noexcept
        {}


        /**
         * @brief 收到数据回调, 在 Session::update() 里调用
         * @return 返回非 0 表示处理失败, Session 会立刻 remove_session()
         */
        virtual int 
        on_data(Session::Ptr, const core::Package*, int) noexcept {
            return 0;
        }
    }; // class IEvent;


    explicit
    Server(const char* host, IEvent* ev) noexcept;


    ~Server() noexcept {
        release();

        for (int i = 0; i < MAX_RECV; ++i) {
            ::mi_free(riovecs_[i].iov_base);
        }
    }


    int
    fd() const noexcept {
        return ufd_;
    }


    uint64_t
    tnow() const noexcept {
        return tnow_;
    }


    std::string
    host() const noexcept {
        return host_;
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
                notify(new core::QEvent(core::QEvent::Type::Stop, nullptr));
            }
        }
    }


    void
    notify(core::QEvent* ev) noexcept {
        ASSERT(evque_.enqueue(std::move(ev)), "事件队列已满");
        bool expected = false;
        if (evflag_.compare_exchange_strong(expected, true)) {
            static constexpr uint64_t event = 1;
            if (::write(evfd_, &event, sizeof(event)) != sizeof(event)) {
                xERROR("write failed: errno = {}, errstr = {}", errno, ::strerror(errno));
            }
        }
    }


private:
    static int
    output(const char *buf, int len, struct IKCPCB*, void *user) noexcept;


    void
    init() noexcept;


    void
    release() noexcept;


    Session::Ptr
    get_session(uint32_t conv) noexcept {
        auto itr = sessions_.find(conv);
        return itr == sessions_.end() ? nullptr : itr->second;
    }


    /**
     * @brief 添加会话,成功后会调用 event_->on_connected() 回调,返回非 0 表示拒绝连接, Server 会立刻移除会话
     */
    int
    add_session(uint32_t conv, Session::Ptr s) noexcept {
        s->set_output(output);
        sessions_.emplace(conv, s);
        if (event_->on_connected(s)) {
            sessions_.erase(conv);
            return -1;
        }
        return 0;
    }


    void
    remove_session(uint32_t conv) noexcept {
        auto itr = sessions_.find(conv);
        if (itr != sessions_.end()) {
            auto kcp = itr->second;
            sessions_.erase(itr);
            event_->on_disconnected(kcp);
        }
    }


    void
    on_event_handle(const ::epoll_event& ev) noexcept;


    void
    on_udp_handle(const ::epoll_event& ev) noexcept;


    void
    on_serv_handle(const ::epoll_event& ev) noexcept;


    void
    on_new_serv(core::QEvent* qe) noexcept;


    void
    add_serv(tcp::Connector::Ptr conn) noexcept {
        ::epoll_event ev;
        ev.data.ptr = conn.get();
        ev.events = EPOLLOUT | EPOLLET;
        ASSERT(::epoll_ctl(epfd_, EPOLL_CTL_ADD, conn->fd(), &ev) == 0, "epoll_ctl add serv fd failed: id = {}, host = {}, errno = {}, errstr = {}", conn->id(), conn->host(), errno, ::strerror(errno));
        servs_.insert(std::make_pair(conn->id(), conn));
    }


    void
    remove_serv(tcp::Connector* conn) noexcept {
        ASSERT(::epoll_ctl(epfd_, EPOLL_CTL_DEL, conn->fd(), nullptr) == 0, "epoll_ctl failed: errno = {}, errstr = {}", errno, ::strerror(errno));
        servs_.erase(conn->id());
    }


    void
    update() noexcept;


    core::SOCKET             ufd_               { core::INVALID_SOCKET };
    core::SOCKET             epfd_              { core::INVALID_SOCKET };
    core::SOCKET             evfd_              { core::INVALID_SOCKET };
    IEvent*                  event_             { nullptr };
    uint64_t                 tnow_              { 0 };
    std::atomic<core::State> state_             { core::State::Stopped };
    std::atomic_bool         evflag_            { false };
    std::string              host_;
    SessionMap               sessions_;
    ::mmsghdr                rmsgs_[MAX_RECV]   {};
    ::iovec                  riovecs_[MAX_RECV] {};
    ::sockaddr_storage       raddrs_[MAX_RECV]  {};
    core::SndBuf::Que        sque_;
    EvQue                    evque_;
    SndBufPool               sb_pool_;
    ServMap                  servs_;
};


} // namespace typhon::kcp;


#endif // __TYPHON_KCP_SERVER_HPP__