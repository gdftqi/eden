#ifndef __TYPHON_TCP_SERVER_HPP__
#define __TYPHON_TCP_SERVER_HPP__


#include "tcp/worker.hpp"


namespace typhon::tcp {


class Server {
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    Server(Server&&) = delete;
    Server& operator=(Server&&) = delete;


public:
    class IEvent {
    public:
        virtual
        ~IEvent() noexcept
        {}


        virtual int
        on_init(Server*) noexcept {
            return 0;
        }


        virtual void
        on_stopped(Server*) noexcept
        {}


        virtual int
        on_connected(Session*) noexcept {
            return 0;
        }


        virtual void
        on_disconnected(Session*) noexcept
        {}


        virtual int
        on_data(Session*, core::Package*, core::PackageTail*) noexcept {
            return 0;
        }
    }; // class IEvent;


    static constexpr int MAX_CONN = 1024 * 8;


    typedef int (*PackageHandler)(Session* s, const core::Package* p) noexcept;


    explicit
    Server(const char* host, IEvent* event) noexcept
        : event_(event)
        , host_(host) {
        ASSERT(event_ != nullptr, "event handler cannot be null");
    }


    ~Server() noexcept {
        for (auto& w: workers_) {
            w->stop();
        }

        for (auto& t: threads_) {
            if (t.joinable()) {
                t.join();
            }
        }

        release();
    }


    std::string
    host() const noexcept {
        return host_;
    }


    uint32_t
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
                static constexpr uint64_t event = 1;
                ASSERT(::write(stop_evfd_, &event, sizeof(event)) == sizeof(event), "errno = {}, errstr = {}", errno, ::strerror(errno));
            }
        }
    }


    Session*
    get_session(core::SOCKET fd) noexcept {
        ASSERT(fd >= 0 && fd < MAX_CONN, "invalid fd: {}", fd);
        return sessions_[fd].get();
    }


    void
    add_session(core::SOCKET fd) noexcept {
        ASSERT(fd >= 0 && fd < MAX_CONN, "invalid fd: {}", fd);
        sessions_[fd] = Session::create(fd, tnow_);
    }


    void
    remove_session(core::SOCKET fd) noexcept {
        ASSERT(fd >= 0 && fd < MAX_CONN, "invalid fd: {}", fd);
        sessions_[fd] = nullptr;
    }


    PackageHandler
    get_handler(uint16_t pkid) const noexcept {
        return handlers[pkid];
    }


    void
    regist_handler(uint16_t pkid, PackageHandler handler) noexcept {
        if (handlers[pkid] != nullptr) {
            xWARN("handler for pk_id {} already exists, will be overwritten", pkid);
        }

        handlers[pkid] = handler;
    }


private:
    void
    init() noexcept;


    void
    release() noexcept;


    int
    on_stop_handle(const ::epoll_event& ev) noexcept;


    int
    on_listen_handle(const ::epoll_event& ev) noexcept;


    int
    on_session_handle(const ::epoll_event& ev) noexcept;


    core::SOCKET                lfd_                  { core::INVALID_SOCKET };
    core::SOCKET                stop_evfd_            { core::INVALID_SOCKET };
    core::SOCKET                epfd_                 { core::INVALID_SOCKET };
    uint32_t                    tnow_                 { 0 };
    IEvent*                     event_                { nullptr };
    std::atomic<core::State>    state_                { core::State::Stopped };
    std::string                 host_;
    std::vector<Worker::Ptr> workers_;
    std::vector<std::thread>    threads_;
    Session::Ptr                sessions_[MAX_CONN]   { nullptr };
    PackageHandler              handlers[UINT16_MAX]  { nullptr };
};

    
} // namespace typhon::tcp


#endif // __TYPHON_TCP_SERVER_HPP__