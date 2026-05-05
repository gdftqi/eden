#include "typhon.in.hpp"

#include <netdb.h>
#include <string.h>
#include <unistd.h>


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