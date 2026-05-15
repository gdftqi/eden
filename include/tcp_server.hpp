#ifndef __TYPHON_TCP_SERVER_HPP__
#define __TYPHON_TCP_SERVER_HPP__


#include "tcp_worker.hpp"


namespace typhon {


class TcpServer {
    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;
    TcpServer(TcpServer&&) = delete;
    TcpServer& operator=(TcpServer&&) = delete;


public:
    explicit
    TcpServer(const char* host) noexcept
        : host_(host) 
    {}


    ~TcpServer() noexcept {
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


    bool
    running() const noexcept {
        return state_.load(std::memory_order_relaxed) == State::Running;
    }


    void
    run() noexcept;


    void
    stop() noexcept {
        if (running()) {
            State expected = State::Running;
            if (state_.compare_exchange_strong(expected, State::Stopping)) {
                static constexpr uint64_t event = 1;
                ASSERT(::write(stop_evfd_, &event, sizeof(event)) == sizeof(event), "errno = {}, errstr = {}", errno, ::strerror(errno));
            }
        }
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


    SOCKET                      lfd_       { INVALID_SOCKET };
    SOCKET                      stop_evfd_ { INVALID_SOCKET };
    SOCKET                      epfd_      { INVALID_SOCKET };
    uint32_t                    tnow_      { 0 };
    std::atomic<State>          state_     { State::Stopped };
    std::string                 host_;
    std::vector<TcpWorker::Ptr> workers_;
    std::vector<std::thread>    threads_;
};

    
} // namespace typhon


#endif // __TYPHON_TCP_SERVER_HPP__