#ifndef __TYPHON_UDP_SERVER_HPP__
#define __TYPHON_UDP_SERVER_HPP__


#include <string>
#include <atomic>
#include <unordered_map>
#include <vector>

#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "kcp.hpp"


namespace typhon {


constexpr int MAX_RECV = 128;
constexpr int MAX_SEND = 32;


class UdpServer {
    UdpServer(const UdpServer&) = delete;
    UdpServer& operator=(const UdpServer&) = delete;
    UdpServer(UdpServer&&) = delete;
    UdpServer& operator=(UdpServer&&) = delete;


    struct SendBuf {
        SendBuf(Kcp* k, const char* b, uint32_t l)
            : kcp(k), buf(const_cast<char*>(b)), len(l)
        {}


        Kcp* kcp;
        void* buf;
        uint32_t len;
    };


public:
    explicit UdpServer(const char* host) noexcept
        : host_(host) {
        for (int i = 0; i < MAX_RECV; ++i) {
            auto hdr = &rmsgs_[i].msg_hdr;
            hdr->msg_iov = &riovecs_[i];
            hdr->msg_iovlen = 1;
            hdr->msg_name = &raddrs_[i];
            hdr->msg_namelen = sizeof(raddrs_[i]);

            riovecs_[i].iov_base = ::malloc(UDP_MTU);
            riovecs_[i].iov_len = UDP_MTU;
        }
    }


    ~UdpServer() noexcept {
        for (int i = 0; i < MAX_RECV; ++i) {
            ::free(riovecs_[i].iov_base);
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
    static int
    output(const char *buf, int len, struct IKCPCB*, void *user) noexcept {
        auto kcp = (Kcp*)user;
        kcp->get_server()->sque_.emplace_back(kcp, buf, len);
        return 0;
    }


    int
    init() noexcept;


    void
    release() noexcept;


    Kcp::Ptr
    get_session(uint32_t conv) noexcept {
        auto itr = sessions_.find(conv);
        return itr == sessions_.end() ? nullptr : itr->second;
    }


    void
    add_session(uint32_t conv, Kcp::Ptr kcp) noexcept {
        kcp->set_output(output);
        sessions_.emplace(conv, std::move(kcp));
    }


    void
    remove_session(uint32_t conv) noexcept {
        sessions_.erase(conv);
    }


    int
    on_event_handle(const ::epoll_event& ev) noexcept;


    int
    on_udp_handle(const ::epoll_event& ev) noexcept;


    void
    update() noexcept;


    SOCKET sockfd_ { INVALID_SOCKET };
    SOCKET epfd_ { INVALID_SOCKET };
    SOCKET evfd_ { INVALID_SOCKET };

    uint32_t tnow_ { 0 };
    std::atomic<State> state_ { State::Stopped };
    std::string host_;
    std::unordered_map<uint32_t, Kcp::Ptr> sessions_;

    ::mmsghdr rmsgs_[MAX_RECV] {};
    ::iovec riovecs_[MAX_RECV] {};
    ::sockaddr_storage raddrs_[MAX_RECV] {};

    std::vector<SendBuf> sque_;
};


} // namespace typhon;


#endif // __TYPHON_UDP_SERVER_HPP__