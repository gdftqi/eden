#include "typhon.in.hpp"


#include <string.h>
#include <unistd.h>


typhon::SOCKET
typhon::tcp_listen(const std::string& host) noexcept {
    std::string host_str(host);
    if (host_str.empty()) {
        return INVALID_SOCKET;
    }

    auto pos = host_str.find_last_of(':');
    std::string port_str;
    if (pos != std::string::npos) {
        port_str = host_str.substr(pos + 1);
        host_str = host_str.substr(0, pos);
    }

    if (port_str.empty()) {
        return INVALID_SOCKET;
    }

    if (host_str.empty()) {
        host_str = "0.0.0.0";
    }

    struct addrinfo hints {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    struct addrinfo* res = nullptr;
    struct addrinfo* rp = nullptr;

    int ret = ::getaddrinfo(host_str.c_str(), port_str.c_str(), &hints, &res);
    if (ret) {
        return INVALID_SOCKET;
    }

    SOCKET lfd = INVALID_SOCKET;
    for (rp = res; rp; rp = rp->ai_next) {
        lfd = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (lfd == INVALID_SOCKET) {
            continue;
        }

        if (set_nonblocking(lfd) < 0) {
            ::close(lfd);
            lfd = INVALID_SOCKET;
            continue;
        }

        int optval = 1;
        if (::setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval))) {
            ::close(lfd);
            lfd = INVALID_SOCKET;
            continue;
        }

        if (!::bind(lfd, rp->ai_addr, rp->ai_addrlen)) {
            break;
        }

        ::close(lfd);
        lfd = INVALID_SOCKET;
    }

    ::freeaddrinfo(res);

    return lfd;
}


typhon::SOCKET
typhon::udp_bind(const std::string& host) noexcept {
    if (host.empty()) {
        return typhon::INVALID_SOCKET;
    }

    auto pos = host.find(':');
    if (pos == std::string::npos) {
        return typhon::INVALID_SOCKET;
    }

    auto ip = host.substr(0, pos);
    auto port = host.substr(pos + 1);

    if (ip.empty()) {
        ip = "0.0.0.0";
    }

    if (port.empty()) {
        return typhon::INVALID_SOCKET;
    }

    auto fd = typhon::INVALID_SOCKET;
    ::addrinfo hints;
    ::addrinfo *result = nullptr, *rp = nullptr;
    ::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;

    if (::getaddrinfo(ip.c_str(), port.c_str(), &hints, &result) != 0) {
        return typhon::INVALID_SOCKET;
    }

    for (rp = result; rp != nullptr; rp = rp->ai_next) {
        fd = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd == typhon::INVALID_SOCKET) {
            continue;
        }

        static constexpr int reuseport = 1;
        static constexpr int sndbuf = 1024 * 1024 * 4;
        static constexpr int rcvbuf = sndbuf * 2;
        if (typhon::set_nonblocking(fd) ||
            ::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &reuseport, sizeof(reuseport)) ||
            ::setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf)) ||
            ::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf))) {
            ::close(fd);
            continue;
        }

        if (::bind(fd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }

        ::close(fd);
    }

    ::freeaddrinfo(result);

    if (rp == nullptr) {
        return typhon::INVALID_SOCKET;
    }

    return fd;
}