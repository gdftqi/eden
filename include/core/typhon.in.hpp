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
#include <simdjson.h>
#include <sodium.h>
#include <yaml-cpp/yaml.h>

#include "core/error.hpp"
#include "utils/cryptor.hpp"
#include "utils/log.hpp"
#include "utils/sys.hpp"


#ifdef SOMAXCONN
#undef SOMAXCONN
#define SOMAXCONN 65535
#endif


namespace typhon::core {


/**
 * @brief UDP MTU UDP 包的MTU
 * @note  单个UDP 包不能超过 UDP_MTU, 否则需要分片发送
 */
constexpr int UDP_MTU = 1450;

/** 
 * @brief Envelope MAC (SipHash tag) 长度, 用来过滤消息
 */
constexpr int ENVELOPE_MAC_LEN = 8;

/**
 * @brief KCP MTU, 用于 KCP 分片
 */
constexpr int KCP_MTU = UDP_MTU - ENVELOPE_MAC_LEN;

/**
 * @brief KCP 协议头长度
 */
constexpr int KCP_HDR_LEN = 24;


typedef int SOCKET;
constexpr SOCKET INVALID_SOCKET = -1;


enum class State: uint8_t {
    Stopped,    // 已停止
    Stopping,   // 正在停止
    Starting,   // 正在启动
    Running,    // 运行时
};


/**
 * @brief 为 fd 设置 non_blocking
 * 
 * @return 成功返回 0, 否则返回 -1
 */
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


/**
 * @brief 创建 UDP fd
 * 
 * @return 成功返回 大于 0 值的 fd, 否则返回 INVALID_SOCKET
 */
SOCKET
udp_bind(const std::string& host, int sndbuf, int rcvbuf) noexcept;


/**
 * @brief 创建TCP监听 fd
 * 
 * @return 成功返回 大于 0 值的 fd, 否则返回 INVALID_SOCKET
 */
SOCKET
tcp_listen(const std::string& host, int sndbuf = 0, int rcvbuf = 0) noexcept;


/**
 * @brief 创建TCP连接 fd
 * 
 * @return 成功返回 大于 0 值的 fd, 否则返回 INVALID_SOCKET
 * 
 * @note 该函数为异步连接.
 */
SOCKET
tcp_connect(const std::string& host, int sndbuf = 0, int rcvbuf = 0) noexcept;


/**
 * @brief 发送 len 长度的字节
 */
ssize_t
writen(SOCKET fd, const void* buf, size_t len) noexcept;


/**
 * @brief sockaddr 转 string 格式
 * 
 * @return 成功返回 sockaddr 的 string 格式, 否则返回空字符串
 */
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


/**
 * @brief 将 IPV4 地址转为 uint32 4 字节
 */
inline uint32_t
sockaddr_to_u32(const sockaddr_in* addr) noexcept {
    return ::ntohl(addr->sin_addr.s_addr);
}


/**
 * @brief 将 uint32_t 4 字节转为 IPV4 地址
 */
inline void
u32_to_sockaddr(sockaddr_in* addr, uint32_t v) noexcept {
    addr->sin_family      = AF_INET;
    addr->sin_port        = 0;
    addr->sin_addr.s_addr = ::htonl(v);
}


struct ServerInfo {
    uint32_t    id      { 0 };
    uint32_t    timeout { 0 };
    std::string protocol;
    std::string name;
    std::string host;
    std::string desc;
    ::time_t    start_time;

    std::string key;
    std::string val;


    std::string
    to_string() const noexcept {
        return std::format("{{\"id\":{},\"protocol\":\"{}\",\"name\":\"{}\",\"host\":\"{}\",\"desc\":\"{}\",\"start_time\":{}}}", 
            id, protocol, name, host, desc, start_time);
    }


    int
    from_yaml(const YAML::Node& root) noexcept;


    int
    from_json(const std::string& json) noexcept {
        simdjson::ondemand::parser parser;
        auto j = simdjson::padded_string(json);
        auto doc = parser.iterate(j);

        if (doc["id"].has_value()) {
            id = doc["id"].get_uint32().value_unsafe();
        }

        if (doc["host"].has_value()) {
            host = std::string(doc["host"].get_string().value_unsafe());
        }

        return 0;
    }
}; // class ServerInfo;


} // namespace typhon::core


#endif // __TYPHON_IN_HPP__