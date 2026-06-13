#ifndef __TYPHON_PACKAGE_HPP__
#define __TYPHON_PACKAGE_HPP__


#include "core/typhon.in.hpp"
#include <type_traits>


namespace typhon::core {


// MAX_HANDLERS: pk_id 字段的合法上界
constexpr int MAX_HANDLERS = 1024;


// =============================================================================
//                          typhon 消息协议 (v1)
// =============================================================================
//
// 字节序: 所有多字节字段一律 big-endian (网络字节序) 本地处理用 host 序.
//
// 单包上限: PKG_MAX_LEN = 65535 是**任意方向 wire frame** 的总长上限.
//
// -----------------------------------------------------------------------------
//  方向与封装格式
// -----------------------------------------------------------------------------
//
//    - 客户端 -> 网关 方向(KCP): Package
//        wire frame = Package 头 (10B) + pk_payload
//        长度由 KCP 消息边界给定，Package 自身**不带长度字段**
//
//    - 网关 -> 后端 方向(TCP): PackageEx
//        wire frame = PackageEx 头 (10B) + 内嵌 Package wire frame
//        网关从 KCP 收到 Package 后，原地 prepend 10B PackageEx 头并填写
//        pke_len / pke_src_id / pke_src_addr，再交给 TCP 发送。
//
//        pke_len = **整个 PackageEx wire frame 总长**(含 PackageEx 头 + 内嵌 Package)
//        后端 TCP 切包流程: peek 2B 取 pke_len -> 读 pke_len 字节 -> 完整 PackageEx.
//
//    业务 payload 上限 = PKG_MAX_LEN - PKG_HDR_EX_LEN - PKG_HDR_LEN
//                      = 65535 - 10 - 10 = 65515
//    更大的载荷由业务层自行分片.
//
// -----------------------------------------------------------------------------
//  字段合法值约定 (v1 协议层强校验, 违反即 drop / 踢 session)
// -----------------------------------------------------------------------------
//
//   pk_id      ∈ [1, MAX_HANDLERS)，即 [1, 1024).
//              - 0 保留为无效值 ("未设置消息号").
//              - >= 1024 超出 tcp::Server handlers[] 索引范围.
//              - kcp::Session::recv 返 -5；tcp::Worker::on_qe_recv_handle 找不到
//                handler 时打 WARN 并跳过 (worker 不会主动踢 session).
//
//   pk_idem    > 0, 单调递增.
//              - 客户端在 session 内自维护单调递增计数器, 每发一包 ++idem.
//              - 0 视为无效包，服务端 recv 返 -6.
//              - 网关侧校验 rcv_idem_ < pk_idem, 违反视为「重复包」丢弃.
//              - 鉴权成功后服务端重置 rcv_idem_=0, 从新 session idem=1 开始计;
//                故单次 session 寿命内 uint32 不会 wrap.
//              - 服务端响应时原样回填客户端 idem 以做 RPC 配对.
//              - 注: idem 校验只在 kcp::Session(客户端↔网关)做; TCP 后端不重复校验.
//
//   pk_dst_id  > 0, 目标服务类型 (路由键, scene / chat / guild / ...).
//              - 0 视为无效包, 服务端 recv 返 -7.
//              - 后端 dispatcher 按 pk_dst_id 选择目标 backend 实例.
//              - 同 service_type 不同实例间，网关用 [sticky by FromPlayerID] 保证粘性.
//
//   pke_len    = PackageEx wire frame 总长(含 PackageEx 头 + 内嵌 Package).
//              - uint16_t, 上限 = PKG_MAX_LEN = 65535.
//              - 网关 stamp 时填写, 后端按此字节数切包.
//
//   pke_src_id = FromPlayerID(网关从 KCP conv -> session 查到的, 客户端无法伪造).
//
//   pke_src_addr = 客户端 IPv4 地址(IPv6 暂不支持).
//
// -----------------------------------------------------------------------------
//  客户端 <-> 网关 (KCP Package)
// -----------------------------------------------------------------------------
//   0                   1                   2                   3
//   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//  |             pk_id             |         pk_idem (hi)          |
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//  |         pk_idem (lo)          |        pk_dst_id (hi)         |
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//  |        pk_dst_id (lo)         |        pk_payload ...         |
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//  |                pk_payload ... (variable)                      |
//  +---------------------------------------------------------------+
//
// -----------------------------------------------------------------------------
//  网关 <-> 业务服 (TCP PackageEx, 网关 prepend 10B 头)
// -----------------------------------------------------------------------------
//   0                   1                   2                   3
//   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//  |            pke_len            |       pke_src_id (hi)         | ┐
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+ │ PackageEx
//  |       pke_src_id (lo)         |      pke_src_addr (hi)        | │ 头 10B
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+ │ 网关 stamp
//  |      pke_src_addr (lo)        |     pke_pk[] (Package) ...    | ┘
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//  |               pke_pk ... (整个 Package wire frame)            |
//  +---------------------------------------------------------------+
//
// =============================================================================


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
constexpr int PKG_MAX_PAYLOAD = PKG_MAX_LEN - PKX_HDR_LEN - PKG_HDR_LEN;  // 65515, pk_payload 上限 (取约束更严的 TCP 方向)

static_assert(PKG_HDR_LEN == 10, "Package header size changed");
static_assert(PKX_HDR_LEN == 10, "PackageEx header size changed");


#define PKID_PING       (100) ///< 心跳 PING
#define PKID_PONG       (101) ///< 心跳 PONG
#define PKID_REGIST_REQ (102) ///< 注册请求
#define PKID_REGIST_RSP (103) ///< 注册应答


} // namespace typhon::core


#endif // __TYPHON_PACKAGE_HPP__