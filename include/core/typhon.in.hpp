#ifndef __TYPHON_IN_HPP__
#define __TYPHON_IN_HPP__


#include <atomic>
#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
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

#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <mimalloc-3.2/mimalloc.h>
#include <sodium.h>

#include "core/error.hpp"
#include "kcp/xkcp.h"
#include "utils/cryptor.hpp"
#include "utils/log.hpp"
#include "utils/sys.hpp"


#ifdef SOMAXCONN
#undef SOMAXCONN
#define SOMAXCONN 65535
#endif


namespace typhon::core {


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


SOCKET
udp_bind(const std::string& host, int sndbuf, int rcvbuf) noexcept;


SOCKET
tcp_listen(const std::string& host, int sndbuf = 0, int rcvbuf = 0) noexcept;


SOCKET
tcp_connect(const std::string& host, int sndbuf = 0, int rcvbuf = 0) noexcept;


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