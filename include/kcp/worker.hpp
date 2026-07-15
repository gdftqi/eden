#ifndef __TYPHON_KCP_WORKER_HPP__
#define __TYPHON_KCP_WORKER_HPP__


#include "core/buffer.hpp"
#include "core/package.hpp"
#include "core/qevent.hpp"
#include "kcp/event.hpp"
#include "kcp/session.hpp"
#include "tcp/connector.hpp"
#include "utils/obj_pool.hpp"
#include "utils/mpsc.hpp"


namespace typhon::kcp {


constexpr int MAX_RECV = 128;
constexpr int MAX_SEND = MAX_RECV;


class Server;


/**
 * @brief KCP 服务器
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
    typedef utils::MPSC<core::QEvent*> EvQue;

    // 发送缓冲区对象池, 用于复用 SndBuf 对象
    typedef utils::ObjPool<core::SndBuf> SndBufPool;

    // 会话映射表, 用于根据 conv 查找 Session, 存放所有会话(包括未鉴权的)
    typedef absl::flat_hash_map<uint32_t, Session::Ptr> SessMap;

    // 后端服务映射表, 用于根据 id 查找 tcp::Connector, 存放所有后端服务
    typedef absl::flat_hash_map<uint32_t, tcp::Connector::Ptr> ServMap;


    static int
    output(const char *buf, int len, struct IKCPCB* kcpcb) noexcept;


    explicit
    Worker(Server* s, int idx) noexcept;


    ~Worker() noexcept;


    int
    index() const noexcept {
        return idx_;
    }


    int
    fd() const noexcept {
        return ufd_;
    }


    uint64_t
    tnow() const noexcept {
        return tnow_;
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
        ASSERT(evque_.enqueue(std::move(ev)), "MPSC 队列已满, 请对队列扩容");
        bool expected = false;
        if (evq_wkring_.compare_exchange_strong(expected, true)) {
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
    add_session(uint32_t conv, Session::Ptr s) noexcept {
        auto [_, res] = sesss_.emplace(conv, s);
        if (res && event_->on_sess_connected(s)) {
            sesss_.erase(conv);
            return -1;
        }
        return 0;
    }


    void
    remove_session(uint32_t conv) noexcept {
        auto sess_itr = sesss_.find(conv);
        if (sess_itr != sesss_.end()) {
            auto sess = sess_itr->second;
            sesss_.erase(sess_itr);
            event_->on_sess_disconnected(sess);
        }
    }


    tcp::Connector::Ptr
    get_serv(uint32_t id) const noexcept {
        auto itr = servs_.find(id);
        return itr == servs_.end() ? nullptr : itr->second;
    }


    void
    add_serv(tcp::Connector::Ptr c) noexcept {
        ::epoll_event ev;
        ev.data.ptr = c.get();
        ev.events = EPOLLOUT | EPOLLET;
        ASSERT(::epoll_ctl(epfd_, EPOLL_CTL_ADD, c->fd(), &ev) == 0, "epoll_ctl add serv fd failed: id = {}, host = {}, errno = {}, errstr = {}", c->id(), c->host(), errno, ::strerror(errno));
        servs_.insert(std::make_pair(c->id(), c));
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
    on_new_serv(core::QEvent* qe) noexcept;


    void
    on_user_send(core::QEvent* qe) noexcept;


    void
    update() noexcept;


    // --------------------------------- 服务侧 ---------------------------------

    void
    on_pong(tcp::Connector::Ptr conn, core::Package *pk) noexcept;


    void
    on_regist_rsp(tcp::Connector::Ptr conn, core::Package *pk) noexcept;


    void
    on_s2c(tcp::Connector::Ptr conn, core::Package *pk) noexcept;


    // --------------------------------- 用户侧 ---------------------------------

    int
    on_regist_req(Session::Ptr s, core::Package *pk) noexcept;


    int
    on_c2s(Session::Ptr s, core::Package *pk) noexcept;


    // --------------------------------- 内部事件 ---------------------------------
    void
    drain_qevent() noexcept;


    // --------------------------------- 基础属性 ---------------------------------
    Server*                  server_ { nullptr };
    IEvent*                  event_  { nullptr };                       ///< 业务回调 (缓存自 server_->event())
    int                      idx_    { -1 };
    core::SOCKET             ufd_    { core::INVALID_SOCKET };  ///< UDP fd
    core::SOCKET             epfd_   { core::INVALID_SOCKET };  ///< epoll fd
    core::SOCKET             evfd_   { core::INVALID_SOCKET };  ///< event fd
    uint64_t                 tnow_   { 0 };                     ///< 当前时间(ms), 系统启动时间
    std::atomic<core::State> state_  { core::State::Stopped };  ///< 状态

    // --------------------------------- recvmmsg 接收属性 ---------------------------------

    ::mmsghdr          rmsgs_[MAX_RECV]   {};
    ::iovec            riovecs_[MAX_RECV] {};
    ::sockaddr_storage raddrs_[MAX_RECV]  {};

    // --------------------------------- 发送属性 ---------------------------------

    core::SndBuf::Que sque_;    ///< 发送队列
    SndBufPool        sb_pool_; ///< 发送缓冲区对象池          

    // --------------------------------- 工作事件属性 ---------------------------------

    std::atomic_bool evq_wkring_ { false };  ///< event queue 队列发送标识
    EvQue            evque_;                   ///< MPSC 事件队列 
    
    // --------------------------------- 会话属性 ---------------------------------

    SessMap sesss_; // 会话侧集合
    ServMap servs_; // 服务侧集合
}; // class Worker;


} // namespace typhon::kcp;


#endif // __TYPHON_KCP_WORKER_HPP__