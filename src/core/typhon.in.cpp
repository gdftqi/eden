#include "core/typhon.in.hpp"
#include "utils/string_ex.hpp"


typhon::core::SOCKET
typhon::core::udp_bind(const std::string& host, int sndbuf, int rcvbuf) noexcept {
    if (host.empty()) {
        return INVALID_SOCKET;
    }

    auto pos = host.find(':');
    if (pos == std::string::npos) {
        return INVALID_SOCKET;
    }

    auto ip = host.substr(0, pos);
    auto port = host.substr(pos + 1);

    if (ip.empty()) {
        ip = "0.0.0.0";
    }

    if (port.empty()) {
        return INVALID_SOCKET;
    }

    auto fd = INVALID_SOCKET;
    ::addrinfo hints;
    ::addrinfo *result = nullptr, *rp = nullptr;
    ::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;

    if (::getaddrinfo(ip.c_str(), port.c_str(), &hints, &result) != 0) {
        return INVALID_SOCKET;
    }

    for (rp = result; rp != nullptr; rp = rp->ai_next) {
        fd = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd == INVALID_SOCKET) {
            continue;
        }

        constexpr int reuseport = 1;
        if (set_nonblocking(fd) ||
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
        return INVALID_SOCKET;
    }

    return fd;
}


typhon::core::SOCKET
typhon::core::tcp_listen(const std::string& host, int sndbuf, int rcvbuf) noexcept {
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

        constexpr int optval = 1;
        if (set_nonblocking(lfd) || ::setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval))) {
            ::close(lfd);
            lfd = INVALID_SOCKET;
            continue;
        }

        if (sndbuf > 0 && rcvbuf > 0) {
            if (::setsockopt(lfd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf)) || ::setsockopt(lfd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf))) {
                ::close(lfd);
                lfd = INVALID_SOCKET;
                continue;
            }
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


typhon::core::SOCKET
typhon::core::tcp_connect(const std::string& host, int sndbuf, int rcvbuf) noexcept {
    if (host.empty()) {
        return INVALID_SOCKET;
    }

    auto pos = host.find(':');
    if (pos == std::string::npos) {
        return INVALID_SOCKET;
    }

    auto ip = host.substr(0, pos);
    auto port = host.substr(pos + 1);

    if (ip.empty()) {
        return INVALID_SOCKET;
    }

    if (port.empty()) {
        return INVALID_SOCKET;
    }

    struct addrinfo hints {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* res = nullptr;
    int ret = ::getaddrinfo(ip.c_str(), port.c_str(), &hints, &res);
    if (ret) {
        return INVALID_SOCKET;
    }

    SOCKET cfd = INVALID_SOCKET;
    for (struct addrinfo* rp = res; rp; rp = rp->ai_next) {
        cfd = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (cfd == INVALID_SOCKET) {
            continue;
        }

        constexpr int on = 1;
        if (set_nonblocking(cfd) < 0 || ::setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on))) {
            ::close(cfd);
            cfd = INVALID_SOCKET;
            continue;
        }

        if (sndbuf > 0 && rcvbuf > 0) {
            if (::setsockopt(cfd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf)) || ::setsockopt(cfd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf))) {
                ::close(cfd);
                cfd = INVALID_SOCKET;
                continue;
            }
        }

        // 非阻塞 socket 上 connect:
        //   rc == 0            -> 立即连上 (localhost 可能)
        //   rc < 0 && EINPROGRESS -> 连接已发起, 正在进行 (跨机最常见),
        //                            保留 fd, 调用方加 epoll 等 EPOLLOUT 确认连上
        //   rc < 0 && 其他      -> 真错误, close 换下一个 addrinfo
        int rc = ::connect(cfd, rp->ai_addr, rp->ai_addrlen);
        if (rc == 0 || (rc < 0 && errno == EINPROGRESS)) {
            break;
        }

        ::close(cfd);
        cfd = INVALID_SOCKET;
    }

    ::freeaddrinfo(res);

    return cfd;
}



ssize_t
typhon::core::writen(SOCKET fd, const void* buf, size_t len) noexcept {
    ssize_t nleft = len, n;
    uint8_t* ptr = (uint8_t*)buf;

    while (nleft > 0) {
        n = ::write(fd, ptr, nleft);
        if (n < 0) {
            int err = errno;
            if (err == EINTR) {
                continue;
            }

            if (err != EWOULDBLOCK && err != EAGAIN) {
                return -err;
            }
            break;
        }
        else if (n == 0) {
            break;
        }

        nleft -= n;
        ptr += n;
    }

    return len - nleft;
}


int
typhon::core::ServerInfo::from_yaml(const YAML::Node& root) noexcept {
    if (!root["id"] || !root["timeout"] || !root["name"] || !root["host"] || !root["desc"] || !root["protocol"]) {
        return -1;
    }

    id         = root["id"].as<uint32_t>();
    timeout    = root["timeout"].as<uint32_t>() * 1000;
    protocol   = root["protocol"].as<std::string>();
    name       = root["name"].as<std::string>();
    host       = root["host"].as<std::string>();
    desc       = root["desc"].as<std::string>();
    start_time = ::time(nullptr);

    key = std::format("/{}/{}", name, id);
    val = to_string();

    return 0;
}