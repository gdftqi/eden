#ifndef __ADAM_IN_HPP__
#define __ADAM_IN_HPP__


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
#include "core/server_info.hpp"
#include "utils/cryptor.hpp"
#include "utils/log.hpp"
#include "utils/sys.hpp"


typedef int SOCKET;
constexpr SOCKET INVALID_SOCKET = -1;


namespace adam::core {


/**
 * @brief UDP MTU UDP 包的MTU
 * @note  单个UDP 包不能超过 UDP_MTU, 否则需要分片发送
 */
constexpr size_t UDP_MTU = 1450;

/**
 * @brief 信封布局(UDP 载荷). KCP 数据报被整个 AEAD 封在里面 --
 * 安全层在最外, 把 KCP 的头也一起裹住, 攻击者改不了 sn/una/wnd/cmd.
 *
 * 偏移 长度 内容
 * 0 8 槽位 SipHash MAC XDP 校验, 覆盖 [8, 32)
 * 8 4 conv 明文 XDP 选密钥槽 + sk_reuseport 分流
 * 12 4 计数器 AEAD nonce + 防重放序号, 每次发送 +1(含重传)
 * 16 N AEAD 密文 明文 = 完整 KCP 数据报(含它自己的 24B 头)
 * 16+N 16 AEAD tag AAD = [8, 16), 即 conv + 计数器
 *
 * @note conv 放在偏移 8 是刻意的: 和改造前 KCP 头里 conv 的位置重合,
 * 两个 BPF 程序(envelope.bpf.c / kcp.bpf.c)因此一个字节都不用改.
 */
constexpr size_t ENVELOPE_MAC_LEN = 8; // 槽位 MAC
constexpr size_t ENVELOPE_CONV_OFF = 8; // 明文 conv 偏移
constexpr size_t ENVELOPE_CTR_OFF = 12; // 计数器偏移
constexpr size_t ENVELOPE_HDR_LEN = 16; // 密文开始的偏移 = MAC + conv + 计数器

/**
 * @brief 信封相对裸 KCP 数据报的总开销 = 头 16 + AEAD tag 16
 * @note utils::XX20_TAG_LEN 也是 16, 这里不引 utils 头, 用字面量并由 session.cpp 静态断言把关
 */
constexpr size_t ENVELOPE_OVERHEAD = ENVELOPE_HDR_LEN + 16;

/**
 * @brief KCP MTU, 用于 KCP 分片, KCP MTU 指的是 KCP 消息头(24字节) + payload 长度
 */
constexpr size_t KCP_MTU = UDP_MTU - ENVELOPE_OVERHEAD;

/**
 * @brief KCP 协议头长度
 */
constexpr size_t KCP_HDR_LEN = 24;


/**
 * @brief 服务状态
 */
enum class State: uint8_t {
    Stopped,    // 已停止
    Stopping,   // 正在停止
    Starting,   // 正在启动
    Running,    // 运行时
}; // enum class State;


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
u32_to_sockaddr(sockaddr_in* addr, uint32_t ip, uint16_t port = 0) noexcept {
    addr->sin_family      = AF_INET;
    addr->sin_port        = port;
    addr->sin_addr.s_addr = ::htonl(ip);
}


inline uint16_t
u16_to_le(uint16_t v) noexcept {
#if (IWORD_BIG_ENDIAN == 1)
    return __builtin_bswap16(v);
#else
    return v;
#endif
}


inline uint32_t
u32_to_le(uint32_t v) noexcept {
#if (IWORD_BIG_ENDIAN == 1)
    return __builtin_bswap32(v);
#else
    return v;
#endif
}


inline uint64_t
u64_to_le(uint64_t v) noexcept {
#if (IWORD_BIG_ENDIAN == 1)
    return __builtin_bswap64(v);
#else
    return v;
#endif
}


} // namespace adam::core


#endif // __ADAM_IN_HPP__
