#ifndef __TYPHON_PACKAGE_HPP__
#define __TYPHON_PACKAGE_HPP__


#include <arpa/inet.h>
#include <inttypes.h>


namespace typhon::core {


// MAX_HANDLERS: pk_id 字段的合法上界 (排他)。
// 1) tcp::Server 用作 handlers[] 数组大小, pk_id 必须 < MAX_HANDLERS。
// 2) kcp::Session::recv 用作 pk_id 合法性校验上界。
// 改动这个常量, 两边自动跟上, 不会漂移。
static constexpr int MAX_HANDLERS = 1024;


// =============================================================================
//                          typhon 消息协议（v1）
// =============================================================================
//
// 字节序：所有多字节字段一律 big-endian（网络字节序）。本地处理用 host 序，
//          发送前调 pk_hton / pke_hton 转换，接收后调 pk_ntoh / pke_ntoh 转回。
//
// 单包上限：PKG_MAX_LEN = 65535 是**任意方向 wire frame** 的总长上限。
//
// -----------------------------------------------------------------------------
//  方向与封装格式
// -----------------------------------------------------------------------------
//
//    - 客户端 → 网关 方向（KCP 上跑）：Package
//        wire frame = Package 头 (10B) + pk_payload
//        长度由 KCP 消息边界给定，Package 自身**不带长度字段**。
//
//    - 网关 → 后端 方向（TCP 上跑）：PackageEx
//        wire frame = PackageEx 头 (10B) + 内嵌 Package wire frame
//        网关从 KCP 收到 Package 后，原地 prepend 10B PackageEx 头并填写
//        pke_len / pke_src_id / pke_src_addr，再交给 TCP 发送。
//
//        pke_len = **整个 PackageEx wire frame 总长**(含 PackageEx 头 + 内嵌 Package)。
//        后端 TCP 切包流程: peek 2B 取 pke_len → 读 pke_len 字节 → 完整 PackageEx。
//
//    业务 payload 上限 = PKG_MAX_LEN - PKG_HDR_EX_LEN - PKG_HDR_LEN
//                      = 65535 - 10 - 10 = 65515
//    更大的载荷由业务层自行分片。
//
// -----------------------------------------------------------------------------
//  字段合法值约定（v1 协议层强校验，违反即 drop / 踢 session）
// -----------------------------------------------------------------------------
//
//   pk_id      ∈ [1, MAX_HANDLERS)，即 [1, 1024)。
//              - 0 保留为无效值（"未设置消息号"）。
//              - >= 1024 超出 tcp::Server handlers[] 索引范围。
//              - kcp::Session::recv 返 -5；tcp::Worker::on_qe_recv_handle 找不到
//                handler 时打 WARN 并跳过（worker 不会主动踢 session）。
//
//   pk_idem    > 0，单调递增。
//              - 客户端在 session 内自维护单调递增计数器，每发一包 ++idem。
//              - 0 视为无效包，服务端 recv 返 -6。
//              - 网关侧校验 rcv_idem_ < pk_idem，违反视为「重复包」丢弃。
//              - 鉴权成功后服务端重置 rcv_idem_=0，从新 session idem=1 开始计；
//                故单次 session 寿命内 uint32 不会 wrap。
//              - 服务端响应时原样回填客户端 idem 以做 RPC 配对。
//              - 注：idem 校验只在 kcp::Session(客户端↔网关)做；TCP 后端不重复校验。
//
//   pk_dst_id  > 0，目标服务类型（路由键，scene / chat / guild / ...）。
//              - 0 视为无效包，服务端 recv 返 -7。
//              - 后端 dispatcher 按 pk_dst_id 选择目标 backend 实例。
//              - 同 service_type 不同实例间，网关用「sticky by FromPlayerID」保证粘性。
//
//   pke_len    = PackageEx wire frame 总长(含 PackageEx 头 + 内嵌 Package)。
//              - uint16_t，上限 = PKG_MAX_LEN = 65535。
//              - 网关 stamp 时填写，后端按此字节数切包。
//
//   pke_src_id = FromPlayerID（网关从 KCP conv → session 查到的，客户端无法伪造）。
//
//   pke_src_addr = 客户端 IPv4 地址（IPv6 暂不支持）。
//
// -----------------------------------------------------------------------------
//  客户端 ↔ 网关（KCP 上跑 Package）
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
//  网关 → 业务服（TCP 上跑 PackageEx，网关 prepend 10B 头）
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
 * @brief 客户端 / 网关 之间的应用层消息头。
 *
 * 详细协议语义(字段合法值、方向语义、字节序)见文件顶部块注释。
 * 这里只列每字段的快速描述,完整约束以顶部为准。
 */
struct Package {
    uint16_t pk_id;        // 业务消息号. 合法范围 [1, MAX_HANDLERS) = [1, 1024).
    uint32_t pk_idem;      // 幂等 ID. **必须 > 0**, 客户端 session 内单调递增.
    uint32_t pk_dst_id;    // 目标服务类型(路由键). **必须 > 0**, 0 视为非法(recv 返 -7).
    uint8_t  pk_payload[]; // 业务 payload. 长度从外层推: KCP 端由消息边界给定;
                           //                              TCP 端 = pke_len - PKG_HDR_EX_LEN - PKG_HDR_LEN.
};


/**
 * @brief 扩展包, 网关 / 后台 之间的应用层消息头.
 *
 * 网关从 KCP 收到 Package 后, prepend 10B PackageEx 头送给后端.
 * 详细协议语义(字段合法值、方向语义、字节序)见文件顶部块注释。
 */
struct PackageEx {
    uint16_t pke_len;       // PackageEx wire frame 总长(含本头 + pke_pk).
    uint32_t pke_src_id;    // FromPlayerID, 网关查表填写, 客户端无法伪造.
    uint32_t pke_src_addr;  // 客户端 IPv4 地址(IPv6 暂不支持).
    uint8_t  pke_pk[];      // 内嵌 Package wire frame (完整 10B 头 + pk_payload).
};


#pragma pack(pop)


/**
 * @defgroup byteorder 字节序转换
 *
 * 协议字节序分层：
 *   - KCP 协议层（conv / sn / ts / ...）：**小端序**，由 ikcp.c 决定，对应用层透明
 *   - Package / PackageEx（应用层）：**大端序（网络字节序）**
 *
 * 两层互不影响 —— KCP 只解析自身头部，Package 字节对 KCP 是 opaque payload。
 *
 * 用法：
 *   - 发送前：本地填 host 序 → 调用 pk_hton / pke_hton
 *   - 接收后：cast 为 Package* / PackageEx* → 调用 pk_ntoh / pke_ntoh → 读字段
 *
 * @note 在大端机上 htons/htonl 是空操作，跨平台自动正确。
 * @warning 转换为**原地修改**；同一字段重复调用一次会自己还原。
 * @warning pke_hton / pke_ntoh **只翻 PackageEx 头自身 3 个字段**，不递归翻内嵌
 *          Package。内嵌 Package 是否需要单独 pk_hton / pk_ntoh 取决于上下文：
 *          - 网关 prepend 头时，内嵌 Package 已是网络序(KCP 直送)，只调 pke_hton 即可。
 *          - 后端解析时，先 pke_ntoh 读 PackageEx 头，再对内嵌 Package 调 pk_ntoh
 *            才能读 pk_id 等字段。
 * @{
 */

/**
 * @brief 本机字节序 → 网络字节序（发送 Package 前调用）
 * @param p 待转换的 Package 头指针
 */
inline void
pk_hton(Package* p) noexcept {
    p->pk_id     = htons(p->pk_id);
    p->pk_idem   = htonl(p->pk_idem);
    p->pk_dst_id = htonl(p->pk_dst_id);
}


/**
 * @brief 网络字节序 → 本机字节序（接收 Package 后调用）
 * @param p 待转换的 Package 头指针
 */
inline void
pk_ntoh(Package* p) noexcept {
    p->pk_id     = ntohs(p->pk_id);
    p->pk_idem   = ntohl(p->pk_idem);
    p->pk_dst_id = ntohl(p->pk_dst_id);
}


/**
 * @brief 本机字节序 → 网络字节序（发送 PackageEx 前调用）
 *
 * **只翻外层 3 个字段**(pke_len / pke_src_id / pke_src_addr)，不递归翻内嵌
 * Package。典型用法：网关 prepend 头时，内嵌 Package 已是网络序(KCP 直送)，
 * 只调 pke_hton 即可。详见 @ref byteorder 的 warning。
 *
 * @param p 待转换的 PackageEx 头指针
 */
inline void
pke_hton(PackageEx* p) noexcept {
    p->pke_len      = htons(p->pke_len);
    p->pke_src_id   = htonl(p->pke_src_id);
    p->pke_src_addr = htonl(p->pke_src_addr);
}


/**
 * @brief 网络字节序 → 本机字节序（接收 PackageEx 后调用）
 *
 * **只翻外层 3 个字段**，不递归翻内嵌 Package。后端解析流程：
 *   1. pke_ntoh(pe) 读 pke_len / pke_src_id / pke_src_addr
 *   2. pk_ntoh((Package*)pe->pke_pk) 才能读内嵌 Package 字段
 *
 * @param p 待转换的 PackageEx 头指针
 */
inline void
pke_ntoh(PackageEx* p) noexcept {
    p->pke_len      = ntohs(p->pke_len);
    p->pke_src_id   = ntohl(p->pke_src_id);
    p->pke_src_addr = ntohl(p->pke_src_addr);
}


inline Package*
pke_get_pk(PackageEx* p) noexcept {
    return (Package*)p->pke_pk;
}


// 协议常量。语义见文件顶部"单包上限"注释。
constexpr int PKG_MAX_LEN     = 65535;                                       // wire frame 总长上限 (任意方向)
constexpr int PKG_HDR_LEN     = sizeof(Package);                             // 10, Package 头长度
constexpr int PKG_HDR_EX_LEN  = sizeof(PackageEx);                           // 10, PackageEx 头长度 (FAM 不计)
constexpr int PKG_MAX_PAYLOAD = PKG_MAX_LEN - PKG_HDR_EX_LEN - PKG_HDR_LEN;  // 65515, pk_payload 上限 (取约束更严的 TCP 方向)

static_assert(PKG_HDR_LEN == 10, "Package header size changed");
static_assert(PKG_HDR_EX_LEN == 10, "PackageEx header size changed");


} // namespace typhon::core


#endif // __TYPHON_PACKAGE_HPP__