#ifndef __ADAM_TCP_REACTOR_HPP__
#define __ADAM_TCP_REACTOR_HPP__


#include "tcp/terminal.hpp"
#include "tcp/message.hpp"
#include "utils/mpsc.hpp"


namespace adam::tcp {


class Server;


/**
 * @brief TCP Reactor
 */
class Reactor {
    Reactor(const Reactor&) = delete;
    Reactor& operator=(const Reactor&) = delete;
    Reactor(Reactor&&) = delete;
    Reactor& operator=(Reactor&&) = delete;


public:
    typedef std::unique_ptr<Reactor> Ptr;
    typedef absl::flat_hash_map<SOCKET, Session::Ptr> SessMap;


    static Ptr
    create(Server* server, uint32_t index) noexcept {
        return Ptr(new Reactor(server, index));
    }


    ~Reactor() noexcept {
        release();
    }


    uint32_t
    index() const noexcept {
        return index_;
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
    Reactor(Server* server, uint32_t index) noexcept
        : server_(server)
        , index_(index)
    {}


    void
    init() noexcept;


    void
    release() noexcept;


    void
    remove_session(SOCKET fd) noexcept;


    Session::Ptr
    get_session(SOCKET fd) noexcept {
        auto itr = sesss_.find(fd);
        return itr == sesss_.end() ? nullptr : itr->second;
    }


    void
    on_event_handle(const ::epoll_event& ev) noexcept;


    void
    on_session_handle(const ::epoll_event& ev) noexcept;


    int
    session_handle(Session::Ptr s) noexcept;


    // 新连接: 注册进本 reactor 的 epoll(ET)+ on_connected 钩子 + 首读(补 ET 注册前到达的数据)
    void
    on_session_connected(Message* m) noexcept;


    void
    check_timeout() noexcept;


    void
    on_ping(Session::Ptr s, core::Package *pk) noexcept;


    void
    on_regist(Session::Ptr s, core::Package *pk) noexcept;


    void
    on_terminal_enter(Session::Ptr s, core::Package *pk) noexcept;


    void
    on_terminal_leave(Session::Ptr s, core::Package *pk) noexcept;


    void
    on_package_handle(Session::Ptr s, core::Package *pk) noexcept;


    void
    drain_evque() noexcept;


    Server*                  server_        { nullptr };
    uint32_t                 index_         { (uint32_t)-1 };
    SOCKET                   epfd_          { INVALID_SOCKET };
    SOCKET                   mfd_           { INVALID_SOCKET };
    uint64_t                 tnow_          { 0 };
    uint64_t                 last_check_ms_ { 0 };
    std::atomic<core::State> state_         { core::State::Stopped };
    std::atomic_bool         mq_workering_  { false };
    utils::MPSC<Message*>    mque_;
    SessMap                  sesss_;
    Terminal::Map            terminal_router_;
}; // class Reactor;


} // namespace adam::tcp


#endif // __ADAM_TCP_REACTOR_HPP__
