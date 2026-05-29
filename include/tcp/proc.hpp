#ifndef __TYPHON_TCP_PROC_HPP__
#define __TYPHON_TCP_PROC_HPP__


#include "core/qevent.hpp"
#include "tcp/session.hpp"
#include "utils/spsc.hpp"


namespace typhon::tcp {


struct RcvArg {
    core::SOCKET fd;
    uint32_t     len;
    uint8_t      data[];
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
            notify(new core::QEvent(core::QEvent::Type::Stop, nullptr));
        }
    }


    void
    notify(core::QEvent* ev) noexcept {
        ASSERT(evque_.enqueue(std::move(ev)), "spsc 队列已满");
        bool expected = false;
        if (evflag_.compare_exchange_strong(expected, true)) {
            static constexpr uint64_t event = 1;
            if (::write(evfd_, &event, sizeof(event)) != sizeof(event)) {
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


    void
    on_event_handle(const ::epoll_event& ev) noexcept;


    void
    on_recv_handle(core::QEvent* qe) noexcept;


    void
    on_send_handle(core::QEvent* qe) noexcept;


    void
    on_add_sess_handle(core::QEvent* qe) noexcept;


    void
    on_rmv_sess_handle(core::QEvent* qe) noexcept;


    void
    check_timeout() noexcept;


    Server*                    server_        { nullptr };
    int                        id_            { -1 };
    core::SOCKET               epfd_          { core::INVALID_SOCKET };
    core::SOCKET               evfd_          { core::INVALID_SOCKET };   // 队列事件
    uint64_t                   tnow_          { 0 };
    uint64_t                   last_check_ms_ { 0 };
    std::atomic<core::State>   state_         { core::State::Stopped };
    std::atomic<bool>          evflag_        { false };
    utils::SPSC<core::QEvent*> evque_;
}; // class Proc;

    
} // namespace typhon::tcp;


#endif // __TYPHON_TCP_PROC_HPP__