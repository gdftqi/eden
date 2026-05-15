#ifndef __TYPHON_TCP_SERVER_HPP__
#define __TYPHON_TCP_SERVER_HPP__


#include "typhon.in.hpp"


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


    std::string
    host() const noexcept {
        return host_;
    }


    bool
    running() const noexcept {
        return state_.load() == State::Running;
    }


    int
    run() noexcept;


    int
    stop() noexcept {
        State expected = State::Running;
        if (state_.compare_exchange_strong(expected, State::Stopping)) {
            static constexpr uint64_t event = 1;
            auto n = ::write(evfd_, &event, sizeof(event));
            if (n != sizeof(event)) {
                return -1;
            }
        }

        return 0;
    }


private:
    int
    init() noexcept;


    void
    release() noexcept;


    int
    on_event_handle(const ::epoll_event& ev) noexcept;


    int
    on_listen_handle(const ::epoll_event& ev) noexcept;


    int
    on_session_handle(const ::epoll_event& ev) noexcept;


    SOCKET   lfd_  { INVALID_SOCKET };
    SOCKET   evfd_ { INVALID_SOCKET };
    SOCKET   epfd_ { INVALID_SOCKET };
    uint32_t tnow_ { 0 };
    std::atomic<State> state_ { State::Stopped };
    std::string host_;
};

    
} // namespace typhon


#endif // __TYPHON_TCP_SERVER_HPP__