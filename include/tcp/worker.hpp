#ifndef __TYPHON_TCP_WORKER_HPP__
#define __TYPHON_TCP_WORKER_HPP__


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


class Worker {
    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;
    Worker(Worker&&) = delete;
    Worker& operator=(Worker&&) = delete;


public:
    typedef std::unique_ptr<Worker> Ptr;


    static Ptr
    create(Server* server) noexcept {
        return Ptr(new Worker(server));
    }

    
    ~Worker() noexcept {
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
    Worker(Server* server) noexcept
        : server_(server)
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


    Server*               server_    { nullptr };
    core::SOCKET             epfd_      { core::INVALID_SOCKET };
    core::SOCKET             que_evfd_  { core::INVALID_SOCKET }; // 队列事件
    core::SOCKET             stop_evfd_ { core::INVALID_SOCKET }; // 停止事件
    std::atomic<core::State> state_     { core::State::Stopped };
    std::atomic<bool>        sending_   { false };
    utils::SPSC<QEvent*>     rque_;
}; // class TcpWorker;

    
} // namespace typhon::tcp;


#endif // __TYPHON_TCP_WORKER_HPP__