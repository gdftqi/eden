#ifndef __ADAM_TCP_PROC_HPP__
#define __ADAM_TCP_PROC_HPP__


#include "tcp/session.hpp"
#include "tcp/message.hpp"
#include "utils/spsc.hpp"


namespace adam::tcp {


class Server;


class Worker {
    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;
    Worker(Worker&&) = delete;
    Worker& operator=(Worker&&) = delete;


public:
    typedef std::unique_ptr<Worker> Ptr;


    static Ptr
    create(Server* server, int id) noexcept {
        return Ptr(new Worker(server, id));
    }

    
    ~Worker() noexcept {
        release();
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
        auto expected = core::State::Running;
        if (state_.compare_exchange_strong(expected, core::State::Stopping)) {
            notify(new Message(Message::Type::Stop));
        }
    }


    void
    notify(Message* m) noexcept {
        ASSERT(mque_.enqueue(std::move(m)), "SPSC 队列已满, 请对队列扩容");
        bool expected = false;
        if (mq_workering_.compare_exchange_strong(expected, true)) {
            constexpr uint64_t event = 1;
            if (::write(mfd_, &event, sizeof(event)) != sizeof(event)) {
                xERROR("write failed: errno = {}, errstr = {}", errno, ::strerror(errno));
            }
        }
    }


private:
    explicit
    Worker(Server* server, int id) noexcept
        : server_(server)
        , id_(id)
    {}


    void
    init() noexcept;


    void
    release() noexcept;


    void
    on_event_handle(const ::epoll_event& ev) noexcept;


    void
    on_recv_handle(Message* m) noexcept;


    void
    on_send_handle(Message* m) noexcept;


    void
    on_add_sess_handle(Message* m) noexcept;


    void
    on_rmv_sess_handle(Message* m) noexcept;


    void
    check_timeout() noexcept;


    void
    on_ping(Session::Ptr s, core::Package *pk) noexcept;


    void
    on_regist(Session::Ptr s, core::Package *pk) noexcept;


    void
    on_handle(Session::Ptr s, core::Package *pk) noexcept;


    void
    drain_evque() noexcept;


    Server*                  server_        { nullptr };
    int                      id_            { -1 };
    SOCKET                   epfd_          { INVALID_SOCKET };
    SOCKET                   mfd_          { INVALID_SOCKET };   // 队列事件
    uint64_t                 tnow_          { 0 };
    uint64_t                 last_check_ms_ { 0 };
    std::atomic<core::State> state_         { core::State::Stopped };
    std::atomic_bool         mq_workering_  { false };
    utils::SPSC<Message*>    mque_;
}; // class Worker;

    
} // namespace adam::tcp


#endif // __ADAM_TCP_PROC_HPP__