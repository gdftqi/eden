#ifndef __TYPHON_PACKAGE_HPP__
#define __TYPHON_PACKAGE_HPP__


#include "core/typhon.in.hpp"
#include <type_traits>


namespace typhon::core {


#define PKID_PING       (100) ///< 心跳 PING
#define PKID_PONG       (101) ///< 心跳 PONG
#define PKID_REGIST_REQ (102) ///< 注册请求
#define PKID_REGIST_RSP (103) ///< 注册应答


#pragma pack(push, 1)


/**
 * @brief 客户端 / 网关 之间的应用层消息头
 */
struct Package {
    uint16_t id;        // 业务消息号, [1, 1024)
    uint32_t seq;       // 消息序号, 必须 > 0, 确保每条消息的唯一性
    uint32_t dst_id;    // 目标服务id (路由键)
    uint8_t  payload[]; // 业务 payload KCP 端由消息边界给定; TCP 端 = len - PKG_HDR_EX_LEN - PKG_HDR_LEN.
};


/**
 * @brief 扩展包, 网关 / 后台 之间的应用层消息头
 */
struct PackageEx {
    uint16_t len;       // PackageEx wire frame 总长(含本头 + pke_pk)
    uint32_t src_id;    // FromPlayerID, 网关查表填写, 客户端无法伪造
    uint32_t src_addr;  // 客户端 IPv4 地址(IPv6 暂不支持)
    uint8_t  pk[];      // 内嵌 Package
};


/**
 * @brief 网关鉴权
 */
struct AuthToken {
    uint64_t expire;     // 过期时间戳
    uint32_t conv;       // 会话 ID
    uint32_t ip;         // 登录IP
    uint8_t  cli_pk[32]; // 客户端 X25519 公钥
    uint8_t  sign[64];   // 登录服 Ed25519 签名
};


#pragma pack(pop)


inline void
pk_hton(Package* p) noexcept {
    p->id     = htons(p->id);
    p->seq   = htonl(p->seq);
    p->dst_id = htonl(p->dst_id);
}


inline void
pk_ntoh(Package* p) noexcept {
    p->id     = ntohs(p->id);
    p->seq   = ntohl(p->seq);
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
    PackageEx* p_ { nullptr };


public:
    explicit
    PKx(void* buf = nullptr) noexcept
        : p_((PackageEx*)buf)
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


    template<typename U = T, std::enable_if_t<std::is_same_v<U, Host>, int> = 0>
    Package*
    pk() const noexcept {
        return (Package*)p_->pk;
    }


    template<typename U = T, std::enable_if_t<std::is_same_v<U, Host>, int> = 0>
    int
    plen() const noexcept {
        constexpr int HDR_SIZE = (int)sizeof(PackageEx) + (int)sizeof(Package);
        return (int)p_->len - HDR_SIZE;
    }
};


inline PKx<Net>
hton(PKx<Host> v) noexcept {
    auto* p       = (PackageEx*)v.raw();
    p->len      = htons(p->len);
    p->src_id   = htonl(p->src_id);
    p->src_addr = htonl(p->src_addr);
    pk_hton((Package*)p->pk);
    return PKx<Net>(p);
}


inline PKx<Host>
ntoh(PKx<Net> v) noexcept {
    auto* p       = (PackageEx*)v.raw();
    p->len      = ntohs(p->len);
    p->src_id   = ntohl(p->src_id);
    p->src_addr = ntohl(p->src_addr);
    pk_ntoh((Package*)p->pk);
    return PKx<Host>(p);
}


template<typename T>
class PK {
    static_assert(std::is_same_v<T, Host> || std::is_same_v<T, Net>, "PK 的 Order 只能是 Host 或 Net");
    Package* p_;
    int      len_;


public:
    static PK
    create(uint16_t id, uint32_t dst_id, const void* payload, int plen) noexcept {
        // 缓冲多留 XX20_TAG_LEN 给加密时附 tag; 但 len_ 只记逻辑长度(HDR + payload, 不含 tag)
        auto size = sizeof(Package) + plen + utils::XX20_TAG_LEN;
        auto buf = ::mi_malloc(size);
        ASSERT(buf != nullptr, "分配内存失败");
        auto* p = (Package*)buf;
        p->id = id;
        p->dst_id = dst_id;
        ::memcpy(p->payload, payload, plen);
        return PK(buf, (int)sizeof(Package) + plen);
    }


    static void
    release(PK& pk) noexcept {
        ::mi_free(pk.raw());
    }


    explicit
    PK() noexcept
        : PK(nullptr, 0)
    {}


    explicit
    PK(void* buf, int len) noexcept
        : p_((Package*)buf)
        , len_(len)
    {}


    uint8_t*
    raw() const noexcept {
        return (uint8_t*)p_;
    }


    int
    plen() const noexcept {
        // len_ 永不含 tag (tag 只在 wire 传输瞬间存在), 故 payload 长度 = len_ - 头
        return p_ ? len_ - (int)sizeof(Package) : 0;
    }


    int
    len() const noexcept {
        return len_;
    }


    template<typename U = T, std::enable_if_t<std::is_same_v<U, Host>, int> = 0>
    Package*
    operator->() const noexcept {
        return p_;
    }
};


inline PK<Net>
hton(PK<Host> v) noexcept {
    pk_hton((Package*)v.raw());
    return PK<Net>(v.raw(), v.len());
}


inline PK<Host>
ntoh(PK<Net> v) noexcept {
    pk_ntoh((Package*)v.raw());
    return PK<Host>(v.raw(), v.len());
}


constexpr int PKG_MAX_LEN     = 65535;                                    // wire frame 总长上限 (任意方向)
constexpr int PKG_HDR_LEN     = sizeof(Package);                          // 10, Package 头长度
constexpr int PKX_HDR_LEN     = sizeof(PackageEx);                        // 10, PackageEx 头长度 (FAM 不计)
// pk_payload 上限, 取**最严**约束: KCP 加密方向 wire = PKG_HDR(10) + payload + tag(16) ≤ PKG_MAX_LEN。
// (TCP 方向 PKX+PKG=20 < 26, 没这严; 此值同时满足两方向。)
constexpr int PKG_MAX_PAYLOAD = PKG_MAX_LEN - PKG_HDR_LEN - (int)utils::XX20_TAG_LEN;  // 65509


static_assert(PKG_HDR_LEN == 10, "Package header size changed");
static_assert(PKX_HDR_LEN == 10, "PackageEx header size changed");


} // namespace typhon::core;


#endif // __TYPHON_PACKAGE_HPP__