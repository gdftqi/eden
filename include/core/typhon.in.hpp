#ifndef __TYPHON_IN_HPP__
#define __TYPHON_IN_HPP__


#include <atomic>
#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <arpa/inet.h>
#include <fcntl.h>
#include <immintrin.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "mimalloc-3.2/mimalloc.h"

#include "utils/log.hpp"


namespace typhon::core {


/**
 * @brief 单个 UDP 数据报 的 payload 上限 (字节).
 *
 * 推算:
 *   IPv6 协议规定的最小 MTU = 1280 (RFC 8200,任何 IPv6 节点必须支持)
 *   - IPv6 头              = 40
 *   - UDP 头               =  8
 *   ─────────────────────────────
 *   UDP payload 安全上限    = 1232
 */
constexpr int UDP_MTU = 1232;
constexpr int ENVELOPE_MAC_LEN = 8; // Envelope MAC (SipHash-2-4 tag) 长度
constexpr int KCP_MTU = UDP_MTU - ENVELOPE_MAC_LEN;


constexpr int KCP_HDR_LEN = 24;


typedef int SOCKET;
constexpr SOCKET INVALID_SOCKET = -1;


enum class State: uint8_t {
    Stopped,
    Stopping,
    Starting,
    Running,
};


inline int
set_nonblocking(int fd) noexcept {
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        return -1;
    }

    if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK)) {
        return -1;
    }

    return 0;
}


inline uint64_t
systime_ms() noexcept{
    struct timespec ts{};
    if (::clock_gettime(CLOCK_MONOTONIC, &ts)) {
        return 0;
    }

    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}


SOCKET
udp_bind(const std::string& host, int sndbuf, int rcvbuf) noexcept;


SOCKET
tcp_listen(const std::string& host, int sndbuf, int rcvbuf) noexcept;


SOCKET
tcp_connect(const std::string& host, int sndbuf, int rcvbuf) noexcept;


ssize_t
writen(SOCKET fd, const void* buf, size_t len) noexcept;


inline std::string
sockaddr_to_string(const sockaddr* addr) noexcept {
    char ip[INET6_ADDRSTRLEN] = {0};
    uint16_t port = 0;

    if (addr->sa_family == AF_INET) {
        auto* v4 = (const sockaddr_in*)addr;
        ::inet_ntop(AF_INET, &v4->sin_addr, ip, sizeof(ip));
        port = ::ntohs(v4->sin_port);
    } else if (addr->sa_family == AF_INET6) {
        auto* v6 = (const sockaddr_in6*)addr;
        ::inet_ntop(AF_INET6, &v6->sin6_addr, ip, sizeof(ip));
        port = ::ntohs(v6->sin6_port);
    } else {
        return "";
    }

    return std::format("{}:{}", ip, port);
}


inline uint32_t
sockaddr_to_u32(const sockaddr_in* addr) noexcept {
    return ::ntohl(addr->sin_addr.s_addr);
}


inline void
u32_to_sockaddr(sockaddr_in* addr, uint32_t v) noexcept {
    addr->sin_family      = AF_INET;
    addr->sin_port        = 0;
    addr->sin_addr.s_addr = ::htonl(v);
}


} // namespace typhon::core;


#endif // __TYPHON_IN_HPP__