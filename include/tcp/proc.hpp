#ifndef __TYPHON_TCP_PROC_HPP__
#define __TYPHON_TCP_PROC_HPP__


#include "tcp/session.hpp"
#include "utils/spsc.hpp"


namespace typhon::tcp {


struct RcvArg {
    core::SOCKET fd;
    uint32_t     len;
    uint8_t      data[];
};


struct QEvent {
    enum Type: uint8_t {
        Recv,       ///< 收到数据
        Send,       ///< 发送数据
        AddSess,    ///< 添加会话
        RmvSess,    ///< 移除会话
    };


    explicit
    QEvent(Type t, void* data) 
        : qe_type(t), qe_data(data) 
    {}


    Type  qe_type; ///< 事件类型
    void* qe_data; ///< 事件数据
};


class Server;


class Proc {
    Proc(const Proc&) = delete;
    Proc& operator=(const Proc&) = delete;
    Proc(Proc&&) = delete;
    Proc& operator=(Proc&&) = delete;


public:
    typedef std::unique_ptr<Proc> Ptr;


    static Ptr
    create(Server* server, int id) noexcept {
        return Ptr(new Proc(server, id));
    }

    
    ~Proc() noexcept {
        release();
    }


    bool
    running() const noexcept {
        return state_.load(std::memory_order_relaxed) == core::State::Running;
    }


    void
    run() noexcept;


    void
    stop() noexcept {
        auto expected = core::State::Running;
        if (state_.compare_exchange_strong(expected, core::State::Stopping)) {
            constexpr uint64_t event = 1;
            auto n = ::write(stop_evfd_, &event, sizeof(event));
            if (n != sizeof(event)) {
                xWARN("write failed: errno = {}, errstr = {}", errno, ::strerror(errno));
            }
        }
    }


    void
    push(QEvent* ev) noexcept {
        ASSERT(rque_.enqueue(std::move(ev)), "spsc 队列已满");
        bool expected = false;
        if (sending_.compare_exchange_strong(expected, true)) {
            constexpr uint64_t event = 1;
            if (::write(que_evfd_, &event, sizeof(event)) != sizeof(event)) {
                xERROR("write failed: errno = {}, errstr = {}", errno, ::strerror(errno));
            }
        }
    }


private:
    explicit
    Proc(Server* server, int id) noexcept
        : server_(server)
        , id_(id)
    {}


    void
    init() noexcept;


    void
    release() noexcept;


    int
    on_stop_handle(const ::epoll_event& ev) noexcept;


    int
    on_que_handle(const ::epoll_event& ev) noexcept;


    int
    on_qe_recv_handle(QEvent* qe) noexcept;


    int
    on_qe_send_handle(QEvent* qe) noexcept;


    int
    on_qe_add_sess_handle(QEvent* qe) noexcept;


    int
    on_qe_rmv_sess_handle(QEvent* qe) noexcept;


    void
    check_timeout() noexcept;


    Server*                  server_         { nullptr };
    int                      id_             { -1 };      
    uint64_t                 last_check_ms_  { 0 };
    core::SOCKET             epfd_           { core::INVALID_SOCKET };
    core::SOCKET             que_evfd_       { core::INVALID_SOCKET };   // 队列事件
    core::SOCKET             stop_evfd_      { core::INVALID_SOCKET };   // 停止事件
    std::atomic<core::State> state_          { core::State::Stopped };
    std::atomic<bool>        sending_        { false };
    utils::SPSC<QEvent*>     rque_;
}; // class Proc;

    
} // namespace typhon::tcp;


#endif // __TYPHON_TCP_PROC_HPP__