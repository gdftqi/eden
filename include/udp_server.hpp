#ifndef __TYPHON_UDP_SERVER_HPP__
#define __TYPHON_UDP_SERVER_HPP__


#include <string>
#include <atomic>

#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "typhon.in.h"


namespace typhon {


constexpr int MAX_RECV = 128;


class UdpServer {
    UdpServer(const UdpServer&) = delete;
    UdpServer& operator=(const UdpServer&) = delete;
    UdpServer(UdpServer&&) = delete;
    UdpServer& operator=(UdpServer&&) = delete;


public:
    explicit UdpServer(const char* host) noexcept 
        : host_(host) {
        for (int i = 0; i < MAX_RECV; ++i) {
            auto hdr = &msgs_[i].msg_hdr;
            hdr->msg_iov = &iovecs_[i];
            hdr->msg_iovlen = 1;
            hdr->msg_name = &addrs_[i];
            hdr->msg_namelen = sizeof(addrs_[i]);

            iovecs_[i].iov_base = ::malloc(UDP_MTU);
            iovecs_[i].iov_len = UDP_MTU;
        }
    }


    ~UdpServer() noexcept {
        for (int i = 0; i < MAX_RECV; ++i) {
            ::free(iovecs_[i].iov_base);
        }
    }


    int
    fd() const noexcept {
        return sockfd_;
    }


    std::string
    host() const noexcept {
        return host_;
    }


    bool
    running() const noexcept {
        return state_.load(std::memory_order_relaxed) == State::Running;
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
    on_udp_handle(const ::epoll_event& ev) noexcept;


    SOCKET sockfd_ { INVALID_SOCKET };
    SOCKET epfd_ { INVALID_SOCKET };
    SOCKET evfd_ { INVALID_SOCKET };

    std::atomic<State> state_ { State::Stopped };
    std::string host_;

    ::mmsghdr msgs_[MAX_RECV] {};
    ::iovec iovecs_[MAX_RECV] {};
    ::sockaddr_storage addrs_[MAX_RECV] {};
};



} // namespace typhon;


#endif // __TYPHON_UDP_SERVER_HPP__