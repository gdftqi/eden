#ifndef __TYPHON_PACKAGE_HPP__
#define __TYPHON_PACKAGE_HPP__


#include "core/typhon.in.hpp"
#include <type_traits>


namespace typhon::core {


#define PKID_PING       (100) ///< 心跳 PING
#define PKID_PONG       (101) ///< 心跳 PONG
#define PKID_REGIST_REQ (102) ///< 注册请求
#define PKID_REGIST_RSP (103) ///< 注册应答
#define PKID_KICK_REQ   (104) ///< 踢人请求
#define PKID_KICK_RSP   (105) ///< 踢人应答
#define PKID_KICK_NTF   (106) ///< 踢人通知
#define PKID_CUSTOM     (200) ///< 自定义消息ID


#pragma pack(push, 1)


/**
 * @brief 客户端 / 网关 之间的应用层消息头
 */
struct Package {
    uint16_t id;        // 业务消息号, [1, 1024)
    uint32_t src_id;    // 源 id
    uint32_t dst_id;    // 目标 id (路由键)
    uint32_t seq;       // 消息序号, 必须 > 0, 确保每条消息的唯一性
    uint8_t  payload[]; // 业务 payload KCP 端由消息边界给定; TCP 端 = len - PKG_HDR_EX_LEN - PKG_HDR_LEN.
};


/**
 * @brief 扩展包, 网关 / 后台 之间的应用层消息头
 */
struct PackageEx {
    uint16_t len;       // PackageEx wire frame 总长(含本头 + pke_pk)
    uint32_t src_addr;  // 客户端 IPv4 地址(IPv6 暂不支持)
    uint8_t  pk[];      // 内嵌 Package
};


/**
 * @brief 网关鉴权
 */
struct AccessToken {
    uint64_t expire;     // 过期时间戳
    uint32_t conv;       // 会话 ID
    uint32_t user_id;    // 用户 ID
    uint32_t ip;         // 登录IP
    uint8_t  cli_pk[32]; // 客户端 X25519 公钥
    uint8_t  sign[64];   // 登录服 Ed25519 签名
};


#pragma pack(pop)


/**
 * @brief 将 Package 的字段从 host 字节序转换为 net 字节序
 */
inline void
pk_hton(Package* p) noexcept {
    p->id     = htons(p->id);
    p->seq    = htonl(p->seq);
    p->dst_id = htonl(p->dst_id);
    p->src_id = htonl(p->src_id);
}


/**
 * @brief 将 Package 的字段从 net 字节序转换为 host 字节序
 */
inline void
pk_ntoh(Package* p) noexcept {
    p->id     = ntohs(p->id);
    p->src_id = ntohl(p->src_id);
    p->seq    = ntohl(p->seq);
    p->dst_id = ntohl(p->dst_id);
}


struct Host {};
struct Net  {};


/**
 * @brief PackageEx 封装类型
 */
template<typename T>
class PKx {
    static_assert(std::is_same_v<T, Host> || std::is_same_v<T, Net>, "PKx 的 Order 只能是 Host 或 Net");


public:
    explicit
    PKx() noexcept
        : PKx(nullptr, 0)
    {}

    explicit
    PKx(void* buf, int size) noexcept
        : p_((PackageEx*)buf)
        , size_(size)
    {}


    uint8_t*
    raw() const noexcept {
        return (uint8_t*)p_; 
    }


    template<typename U = T, std::enable_if_t<std::is_same_v<U, Host>, int> = 0>
    PackageEx*
    operator->() const noexcept {
        return p_;
    }


    template<typename U = T, std::enable_if_t<std::is_same_v<U, Net>, int> = 0>
    PackageEx*
    operator->() const noexcept {
        return p_;
    }


    template<typename U = T, std::enable_if_t<std::is_same_v<U, Host>, int> = 0>
    Package*
    pk() const noexcept {
        return (Package*)p_->pk;
    }


    /**
     * @brief 返回 payload 长度, 不含 tag 长度
     */
    template<typename U = T, std::enable_if_t<std::is_same_v<U, Host>, int> = 0>
    int
    payload_len() const noexcept {
        constexpr int HDR_SIZE = (int)sizeof(PackageEx) + (int)sizeof(Package);
        return (int)p_->len - HDR_SIZE;
    }


    /**
     * @brief 返回总长度, 包含头和 payload
     */
    template<typename U = T, std::enable_if_t<std::is_same_v<U, Host>, int> = 0>
    int
    size() const noexcept {
        return size_;
    }


    /**
     * @brief 返回总长度, 包含头和 payload
     */
    template<typename U = T, std::enable_if_t<std::is_same_v<U, Net>, int> = 0>
    int
    size() const noexcept {
        return size_;
    }


private:
    PackageEx* p_    { nullptr };
    int        size_ { 0 };
}; // class PKx<T>;


/**
 * @brief 将 PKx<Host> 转为 PKx<Net>
 * 
 * @note 入参 v 也会被转换为 net 字节序, 但不会修改 v 的类型
 */
inline PKx<Net>
hton(PKx<Host> v) noexcept {
    auto* p     = v.operator->();
    p->len      = htons(p->len);
    p->src_addr = htonl(p->src_addr);
    pk_hton((Package*)p->pk);
    return PKx<Net>(p, v.size());
}


/**
 * @brief 将 PKx<Net> 转为 PKx<Host>
 * 
 * @note 入参 v 也会被转换为 host 字节序, 但不会修改 v 的类型
 */
inline PKx<Host>
ntoh(PKx<Net> v) noexcept {
    auto* p     = v.operator->();
    p->len      = ntohs(p->len);
    p->src_addr = ntohl(p->src_addr);
    pk_ntoh((Package*)p->pk);
    return PKx<Host>(p, v.size());
}


template<typename T>
class PK {
    static_assert(std::is_same_v<T, Host> || std::is_same_v<T, Net>, "PK 的 Order 只能是 Host 或 Net");


public:
    /**
     * 创建 PK 对象, 由调用者负责释放内存, 释放时请调用 release(PK& pk)
     */
    static PK
    create(uint16_t id, uint32_t src_id, uint32_t dst_id, const void* payload, int payload_len) noexcept {
        // 缓冲多留 XX20_TAG_LEN(16字节) 用于 chacha20-poly1305 tag部分.
        // 但 PK.len_ 字段 只存逻辑长度(HDR + payload, 不含 tag)
        auto size = sizeof(Package) + payload_len + utils::XX20_TAG_LEN;
        auto buf  = ::mi_malloc(size);
        ASSERT(buf != nullptr, "分配内存失败");

        auto* p   = (Package*)buf;
        p->id     = id;
        p->dst_id = dst_id;
        p->src_id = src_id;
        ::memcpy(p->payload, payload, payload_len);
        return PK(buf, (int)sizeof(Package) + payload_len);
    }


    /**
     * @brief 释放 PK 对象占用的内存
     */
    static void
    release(PK& pk) noexcept {
        ::mi_free(pk.raw());
    }


    /**
     * @brief 构造函数, 创建一个空的 PK 对象, 仅用于占位, 不可访问其成员
     */
    explicit
    PK() noexcept
        : PK(nullptr, 0)
    {}


    /**
     * @brief 构造函数
     */
    explicit
    PK(void* buf, int len) noexcept
        : p_((Package*)buf)
        , size_(len)
    {}


    /**
     * @brief 返回原始指针, 由调用者负责释放内存, 释放时请调用 release(PK& pk)
     */
    uint8_t*
    raw() const noexcept {
        return (uint8_t*)p_;
    }


    /**
     * @brief payload 长度, 不含 tag 长度
     */
    int
    payload_len() const noexcept {
        // len_ 永不含 tag (tag 只在 wire 传输瞬间存在), 故 payload 长度 = len_ - 头
        return p_ ? size_ - (int)sizeof(Package) : 0;
    }


    /**
     * @brief 返回总长度, 包含头和 payload
     */
    int
    size() const noexcept {
        return size_;
    }


    template<typename U = T, std::enable_if_t<std::is_same_v<U, Host>, int> = 0>
    Package*
    operator->() const noexcept {
        return p_;
    }


private:
    Package* p_;
    int      size_;
}; // class PK<T>;


inline PK<Net>
hton(PK<Host> v) noexcept {
    pk_hton((Package*)v.raw());
    return PK<Net>(v.raw(), v.size());
}


inline PK<Host>
ntoh(PK<Net> v) noexcept {
    pk_ntoh((Package*)v.raw());
    return PK<Host>(v.raw(), v.size());
}


constexpr int PKG_MAX_LEN = 65535;                // 最大支持的消息长度
constexpr int PKG_HDR_LEN = sizeof(Package);      // 14 Bytes, Package 头长度
constexpr int PKX_HDR_LEN = sizeof(PackageEx);    // 6 Bytes, PackageEx 头长度

/**
 * @brief pk_payload 上限
 */ 
constexpr int PKG_MAX_PAYLOAD = PKG_MAX_LEN - PKG_HDR_LEN - (int)utils::XX20_TAG_LEN;
} // namespace typhon::core;


#endif // __TYPHON_PACKAGE_HPP__