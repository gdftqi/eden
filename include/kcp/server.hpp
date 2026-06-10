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
 * @brief KCP 服务器
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
    typedef std::unordered_map<uint32_t, Session::Ptr>        UserMap;
    typedef std::unordered_map<uint32_t, tcp::Connector::Ptr> ServMap;

    
    /**
     * @brief KCP 服务事件回调接口.
     */
    class IEvent {
    public:
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
         * @brief 会话连接回调, 在 Server::add_session() 成功后调用
         * @return 返回非 0 表示拒绝连接, Server 会立刻 remove_session()
         */
        virtual int 
        on_connected(Session::Ptr) noexcept {
            return 0;
        }


        /**
         * @brief 会话断开回调, 在 Server::remove_session() 里调用
         */
        virtual void 
        on_disconnected(Session::Ptr) noexcept
        {}
    }; // class IEvent;


    explicit
    Server(const char* host, IEvent* ev) noexcept;


    ~Server() noexcept;


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


    std::string
    to_string() const noexcept {
        return desc_;
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
                notify(new core::QEvent(core::QEvent::Type::Stop));
            }
        }
    }


    void
    notify(core::QEvent* ev) noexcept {
        ASSERT(evque_.enqueue(std::move(ev)), "事件队列已满");
        bool expected = false;
        if (evflag_.compare_exchange_strong(expected, true)) {
            constexpr uint64_t event = 1;
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
        auto itr = users_.find(conv);
        return itr == users_.end() ? nullptr : itr->second;
    }


    /**
     * @brief 添加会话,成功后会调用 event_->on_connected() 回调.
     * @return 成功返回 0, 否则返回 -1
     */
    int
    add_session(uint32_t conv, Session::Ptr s) noexcept {
        s->set_output(output);
        users_.emplace(conv, s);
        if (event_->on_connected(s)) {
            users_.erase(conv);
            return -1;
        }
        return 0;
    }


    void
    remove_session(uint32_t conv) noexcept {
        auto itr = users_.find(conv);
        if (itr != users_.end()) {
            auto kcp = itr->second;
            users_.erase(itr);
            event_->on_disconnected(kcp);
        }
    }


    tcp::Connector::Ptr
    get_serv(uint32_t id) const noexcept {
        auto itr = servs_.find(id);
        return itr == servs_.end() ? nullptr : itr->second;
    }


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
    on_event_handle(const ::epoll_event& ev) noexcept;


    void
    on_udp_handle(const ::epoll_event& ev) noexcept;


    void
    on_serv_handle(const ::epoll_event& ev) noexcept;


    void
    on_new_serv(core::QEvent* qe) noexcept;


    void
    update() noexcept;


    // --------------------------------- 服务侧 ---------------------------------

    void
    on_pong(tcp::Connector* conn, core::PKx<core::Host> &pkx) noexcept;


    void
    on_regist_rsp(tcp::Connector* conn, core::PKx<core::Host> &pkx) noexcept;


    void
    on_s2c(tcp::Connector* conn, core::PKx<core::Host> &pkx) noexcept;


    // --------------------------------- 用户侧 ---------------------------------

    int
    on_ping(Session::Ptr s, core::PK<core::Host> &pk) noexcept;


    int
    on_regist_req(Session::Ptr s, core::PK<core::Host> &pk) noexcept;


    int
    on_c2s(Session::Ptr s, core::PK<core::Host> &pk) noexcept;


    // --------------------------------- 基础属性 ---------------------------------

    core::SOCKET             ufd_   { core::INVALID_SOCKET };  ///< UDP fd
    core::SOCKET             epfd_  { core::INVALID_SOCKET };  ///< epoll fd
    core::SOCKET             evfd_  { core::INVALID_SOCKET };  ///< event fd
    uint64_t                 tnow_  { 0 };                     ///< 当前时间(ms), 系统启动时间
    IEvent*                  event_ { nullptr };               ///< 服务事件
    std::atomic<core::State> state_ { core::State::Stopped };  ///< 状态
    std::string              host_;
    std::string              desc_;

    // --------------------------------- recvmmsg 接收属性 ---------------------------------

    ::mmsghdr          rmsgs_[MAX_RECV]   {};
    ::iovec            riovecs_[MAX_RECV] {};
    ::sockaddr_storage raddrs_[MAX_RECV]  {};

    // --------------------------------- 发送属性 ---------------------------------

    core::SndBuf::Que sque_;    ///< 发送队列
    SndBufPool        sb_pool_; ///< 发送缓冲区对象池          

    // --------------------------------- 工作事件属性 ---------------------------------

    std::atomic_bool evflag_ { false };  ///< event queue 队列发送标识
    EvQue            evque_;             ///< SPSC 事件队列 
    
    // --------------------------------- 会话属性 ---------------------------------

    UserMap users_; // 用户侧集合
    ServMap servs_;    // 服务侧集合
}; // class Server;


} // namespace typhon::kcp;


#endif // __TYPHON_KCP_SERVER_HPP__