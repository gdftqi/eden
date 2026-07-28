#ifndef __ADAM_KCP_WORKER_HPP__
#define __ADAM_KCP_WORKER_HPP__


#include "core/package.hpp"
#include "kcp/datagram.hpp"
#include "kcp/message.hpp"
#include "kcp/session.hpp"
#include "tcp/connector.hpp"
#include "utils/obj_pool.hpp"
#include "utils/mpsc.hpp"


namespace adam::kcp {


class IHook;
class Server;


/**
 * @brief Kcp Worker
 * @note 线程不安全, 必须在单线程中使用
 */
class Worker {
    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;
    Worker(Worker&&) = delete;
    Worker& operator=(Worker&&) = delete;


public:
    typedef std::unique_ptr<Worker> Ptr;

    // 事件队列, 用于跨线程传递事件
    typedef utils::MPSC<Message*> MsgQue;

    // 发送缓冲区对象池, 用于复用 SndBuf 对象
    typedef utils::ObjPool<Datagram> DatagramPool;

    // 会话映射表, 用于根据 conv 查找 Session, 存放所有会话(包括未鉴权的)
    typedef absl::flat_hash_map<uint32_t, Session::Ptr> SessMap;

    // 后端服务映射表, 用于根据 id 查找 tcp::Connector, 存放所有后端服务
    typedef absl::flat_hash_map<uint32_t, tcp::Connector::Ptr> ServMap;
    typedef absl::flat_hash_set<uint32_t>                      ServSet;
    typedef std::vector<uint32_t>                              RouterIDSet;


    /**
     * @brief closing 状态(借鉴 QUIC): 会话已被踢除并销毁, 但保留一份 KICK 报文,
     *        对端若仍在发包(说明它没收到)就原样回一次, 直到过期。
     *        既避免"丢一次就永远不知道", 又不必让半死会话占着 KCP 资源。
     */
    struct Closing {
        ::sockaddr_storage addr    {};
        ::socklen_t        addrlen { 0 };
        uint32_t           len     { 0 };   // KICK 报文长度(含 8B MAC 信封)
        uint64_t           expire  { 0 };   // 到期即丢弃
        uint8_t            buf[core::ENVELOPE_MAC_LEN + 64] {};
    };

    typedef absl::flat_hash_map<uint32_t, Closing> ClosingMap;


    /**
     * @brief ikcp 发送回调
     */
    static int
    output(const char *buf, int len, struct IKCPCB* kcpcb) noexcept;


    /**
     * @brief 构造函数
     */
    explicit
    Worker(Server* s, int idx) noexcept;


    /**
     * @brief 析构函数
     */
    ~Worker() noexcept;


    /**
     * @brief 属性 kcp server 的下标, 用于确认 session 的发送线程
     */
    int
    index() const noexcept {
        return index_;
    }


    /**
     * @brief UDP 套接字
     */
    int
    fd() const noexcept {
        return ufd_;
    }


    /**
     * @brief 当前时间 (系统运行时间, 而非 unix 时间戳, 单位ms)
     */
    uint64_t
    tnow() const noexcept {
        return tnow_;
    }


    // 路由服务 id 集合
    const RouterIDSet&
    router_ids() const noexcept {
        return router_ids_;
    }


    // 按 id 取后端连接
    tcp::Connector::Ptr
    get_serv(uint32_t id) const noexcept {
        auto itr = servs_.find(id);
        return itr == servs_.end() ? nullptr : itr->second;
    }


    /**
     * @brief 是否运行中
     */
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
        if (running()) {
            core::State expected = core::State::Running;
            if (state_.compare_exchange_strong(expected, core::State::Stopping)) {
                notify(new Message(Message::Type::Stop));
            }
        }
    }


    /**
     * @brief 事件通知, 该函数可在多线程中调用, 线程安全.
     */
    void
    notify(Message* m) noexcept {
        ASSERT(mque_.enqueue(std::move(m)), "MPSC 队列已满, 请对队列扩容");
        bool expected = false;
        if (mq_workring_.compare_exchange_strong(expected, true)) {
            constexpr uint64_t event = 1;
            if (::write(evfd_, &event, sizeof(event)) != sizeof(event)) {
                xERROR("write failed: errno = {}, errstr = {}", errno, ::strerror(errno));
            }
        }
    }


private:
    void
    init() noexcept;


    void
    release() noexcept;


    Session::Ptr
    get_session(uint32_t conv) noexcept {
        auto itr = sesss_.find(conv);
        return itr == sesss_.end() ? nullptr : itr->second;
    }


    /**
     * @brief 添加会话,成功后会调用 event_->on_connected() 回调.
     * @return 成功返回 0, 否则返回 -1
     */
    int
    add_session(uint32_t conv, Session::Ptr s) noexcept;


    void
    remove_session(uint32_t conv, uint32_t code) noexcept;


    // 踢除会话: 发 KICK 给客户端 → 快照报文进 closing 表 → 摘会话
    void
    kick_session(uint32_t conv, uint32_t code) noexcept;


    void
    add_backend(tcp::Connector::Ptr c) noexcept {
        ::epoll_event ev;
        ev.data.ptr = c.get();
        ev.events = EPOLLOUT | EPOLLET;
        ASSERT(::epoll_ctl(epfd_, EPOLL_CTL_ADD, c->fd(), &ev) == 0, "epoll_ctl add serv fd failed: id = {}, host = {}, errno = {}, errstr = {}", c->id(), c->host(), errno, ::strerror(errno));
        servs_.emplace(c->id(), c);
    }


    void
    remove_serv(tcp::Connector::Ptr c) noexcept;


    void
    on_event_handle(const ::epoll_event& ev) noexcept;


    void
    on_udp_handle(const ::epoll_event& ev) noexcept;


    void
    on_serv_handle(const ::epoll_event& ev) noexcept;


    void
    on_ensure_backend(Message* m) noexcept;


    void
    on_forward_to_session(Message* m) noexcept;


    void
    update() noexcept;


    void
    drain_qevent() noexcept;


    // --------------------------------- 服务侧 ---------------------------------

    void
    on_pong(tcp::Connector::Ptr conn, core::Package *pk) noexcept;


    void
    on_regist_backend_rsp(tcp::Connector::Ptr conn, core::Package *pk) noexcept;


    void
    on_terminal_kick_notify(tcp::Connector::Ptr conn, core::Package *pk) noexcept;


    void
    on_terminal_bind_notify(tcp::Connector::Ptr conn, core::Package *pk) noexcept;


    void
    on_terminal_unbind_notify(tcp::Connector::Ptr conn, core::Package *pk) noexcept;


    void
    on_terminal_enter_rsp(tcp::Connector::Ptr conn, core::Package *pk) noexcept;


    // 路由服务(重)注册成功 → 把本 worker 名下、归属该实例的终端全量重报
    void
    terminal_reenter(uint32_t rid) noexcept;


    void
    on_s2c(tcp::Connector::Ptr conn, core::Package *pk) noexcept;


    // --------------------------------- 用户侧 ---------------------------------

    int
    on_regist_terminal_req(Session::Ptr s, core::Package *pk) noexcept;


    int
    on_c2s(Session::Ptr s, core::Package *pk) noexcept;


    int
    on_pack_handle(Session::Ptr s, core::Package* pk) noexcept;


    // ------------------------------------------------------------------
    // 基础属性
    // ------------------------------------------------------------------

    Server*                  server_ { nullptr };
    IHook*                   event_  { nullptr };               // 业务回调 (缓存自 server_->event())
    int                      index_  { -1 };                    // 在 kcp server 中的所属下标
    SOCKET                   ufd_    { INVALID_SOCKET };        // UDP fd
    SOCKET                   epfd_   { INVALID_SOCKET };        // epoll fd
    SOCKET                   evfd_   { INVALID_SOCKET };        // event fd
    uint64_t                 tnow_   { 0 };                     // 当前时间(ms), 系统启动时间
    std::atomic<core::State> state_  { core::State::Stopped };  // 状态

    // ------------------------------------------------------------------
    // recvmmsg 接收属性
    // ------------------------------------------------------------------

    // recvmmsg 最大接收值
    static constexpr int MAX_RECV = 128;

    // sendmmsg 最大发送值
    static constexpr int MAX_SEND = MAX_RECV;

    ::mmsghdr          rmsgs_[MAX_RECV]   {}; // recvmmsg 参数
    ::iovec            riovecs_[MAX_RECV] {}; // recvmmsg iov 数据
    ::sockaddr_storage raddrs_[MAX_RECV]  {}; // 对端地址集

    // ------------------------------------------------------------------
    // 发送属性
    // ------------------------------------------------------------------

    Datagram::Que dg_que_;    // 发送队列
    DatagramPool  dg_pool_; // 发送缓冲区对象池     s     

    // ------------------------------------------------------------------
    // 事件属性
    // ------------------------------------------------------------------

    std::atomic_bool mq_workring_ { false };  // event queue 队列发送标识
    MsgQue           mque_;                   // MPSC 事件队列 
    
    // ------------------------------------------------------------------
    // 会话属性
    // ------------------------------------------------------------------

    SessMap     sesss_;      // 会话侧集合
    RouterIDSet router_ids_; // 路由服务集, 这里只存路由服务的id
    ClosingMap  closing_;    // conv → 待重发的 KICK 报文(见 Closing 注释)
    ServMap     servs_;      // 服务侧集合
}; // class Worker;


} // namespace adam::kcp;


#endif // __ADAM_KCP_WORKER_HPP__
