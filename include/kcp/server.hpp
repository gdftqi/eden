#ifndef __TYPHON_KCP_SERVER_HPP__
#define __TYPHON_KCP_SERVER_HPP__


#include "core/buffer.hpp"
#include "core/package.hpp"
#include "kcp/session.hpp"
#include "utils/obj_pool.hpp"


namespace typhon::kcp {


constexpr int MAX_RECV = 128;
constexpr int MAX_SEND = MAX_RECV;


/**
 * @brief KCP 协议服务器
 * @note 线程不安全，必须在单线程中使用
 */
class Server {
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    Server(Server&&) = delete;
    Server& operator=(Server&&) = delete;


public:
    typedef std::unique_ptr<Server> Ptr;
    typedef utils::ObjPool<core::SndBuf> SndBufPool;
    typedef std::unordered_map<uint32_t, Session::Ptr> SessionMap;

    
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
        on_data(Session::Ptr, const core::Package*) noexcept {
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


    uint32_t
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
                static constexpr uint64_t event = 1;
                ASSERT(::write(stop_evfd_, &event, sizeof(event)) == sizeof(event), "errno = {}, errstr = {}", errno, ::strerror(errno));
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
    on_stop_handle(const ::epoll_event& ev) noexcept;


    void
    on_udp_handle(const ::epoll_event& ev) noexcept;


    void
    update() noexcept;


    core::SOCKET             ufd_               { core::INVALID_SOCKET };
    core::SOCKET             epfd_              { core::INVALID_SOCKET };
    core::SOCKET             stop_evfd_         { core::INVALID_SOCKET };
    IEvent*                  event_             { nullptr };
    uint32_t                 tnow_              { 0 };
    std::atomic<core::State> state_             { core::State::Stopped };
    std::string              host_;
    SessionMap               sessions_;
    ::mmsghdr                rmsgs_[MAX_RECV]   {};
    ::iovec                  riovecs_[MAX_RECV] {};
    ::sockaddr_storage       raddrs_[MAX_RECV]  {};
    core::SndBuf::Que        sque_;
    SndBufPool               sb_pool_;
};


} // namespace typhon::kcp;


#endif // __TYPHON_KCP_SERVER_HPP__