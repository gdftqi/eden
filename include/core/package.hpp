#ifndef __TYPHON_PACKAGE_HPP__
#define __TYPHON_PACKAGE_HPP__


#include <arpa/inet.h>
#include <inttypes.h>


namespace typhon::core {


// MAX_HANDLERS: pk_id 字段的合法上界 (排他)。
// 1) tcp::Server 用作 handlers[] 数组大小,pk_id 必须 < MAX_HANDLERS。
// 2) kcp::Session::recv_pk 用作 pk_id 合法性校验上界。
// 改动这个常量,两边自动跟上,不会漂移。
static constexpr int MAX_HANDLERS = 1024;


// =============================================================================
//                          typhon 消息协议（v1）
// =============================================================================
//
// 字节序：所有多字节字段一律 big-endian（网络字节序）。本地处理用 host 序，
//          发送前调 pk_hton / pkt_hton 转换，接收后调 pk_ntoh / pkt_ntoh 转回。
// 单包上限：PKG_MAX_LEN = 65535 是**任意方向 wire frame** 的总长上限。
//
//  **pk_len 字段语义随方向变化**（重要！）：
//
//    - 客户端 → 网关 方向（KCP 上跑）：
//        pk_len = header + pk_data，**不含 PackageTail**（客户端填写）
//        最大值 = PKG_MAX_LEN - PKG_TAIL_LEN = 65527
//
//    - 网关 → 后端 方向（TCP 上跑）：
//        网关追加 PackageTail 时**重写 pk_len = header + pk_data + tail**
//        最大值 = PKG_MAX_LEN = 65535
//        后端按 pk_len 读完整字节即为 "Package + Tail" 完整 wire frame。
//
//    业务 payload (pk_data) 上限统一 = PKG_MAX_DATA_LEN = 65515
//    更大的载荷由业务层自行分片。
//
// -----------------------------------------------------------------------------
//  字段合法值约定（v1 协议层强校验，违反即 drop / 踢 session）
// -----------------------------------------------------------------------------
//
//   pk_id      ∈ [1, MAX_HANDLERS)，即 [1, 1024)。
//              - 0 保留为无效值（"未设置消息号"）。
//              - >= 1024 超出 tcp::Server handlers[] 索引范围。
//              - kcp::Session::recv_pk 返 -5；tcp::Worker::on_qe_recv_handle 找不到
//                handler 时打 WARN 并跳过（worker 不会主动踢 session）。
//
//   pk_idem    > 0，单调递增。
//              - 客户端在 session 内自维护单调递增计数器，每发一包 ++idem。
//              - 0 视为无效包，服务端 recv_pk 返 -6。
//              - 服务端校验 rcv_idem_ < pk_idem，违反视为「重复包」（rcv_pk 返 0）丢弃。
//              - 鉴权成功后服务端重置 rcv_idem_=0，从新 session idem=1 开始计；
//                故单次 session 寿命内 uint32 不会 wrap。
//              - 服务端响应时原样回填客户端 idem 以做 RPC 配对。
//
//   pk_dst_id  > 0，目标服务类型（路由键，scene / chat / guild / ...）。
//              - 0 视为无效包，服务端 recv_pk 返 -7。
//              - 后端 dispatcher 按 pk_dst_id 选择目标 backend 实例。
//              - 同 service_type 不同实例间，网关用「sticky by FromPlayerID」保证粘性。
//
// -----------------------------------------------------------------------------
//  客户端 ↔ 网关（KCP 上面跑的是这个）
// -----------------------------------------------------------------------------
//   0                   1                   2                   3
//   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//  |             pk_len            |             pk_id             |
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//  |                           pk_idem                             |
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//  |                          pk_dst_id                            |
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//  |                    pk_data ... (variable)                     |
//  +---------------------------------------------------------------+
//
// -----------------------------------------------------------------------------
//  网关 → 业务服（网关 stamp PackageTail，并**重写 pk_len 含 tail**）
// -----------------------------------------------------------------------------
//  +-------- Package (12B 头 + pk_data，pk_len 已含 tail) ----------+
//  | ...                                                           |
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//  |                         pkt_src_id                            | ┐ PackageTail
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+ │ 8 bytes,
//  |                        pkt_src_addr                           | │ 网关 stamp
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+ ┘
//
//   后端切包：直接读 pk_len 字节即拿到 "完整 Package + Tail"。
//             tail 位于 (buf + pk_len - 8)，即整体末尾 8 字节。
//   pkt_src_id   = FromPlayerID（网关从 conv→session 查到的，客户端无法伪造）
//   pkt_src_addr = 客户端 IPv4 地址（IPv6 暂不支持）
//
// =============================================================================


#pragma pack(push, 1)


/**
 * @brief 客户端 / 网关 / 后端之间的应用层消息头。
 *
 * 详细协议语义(字段合法值、方向语义、字节序)见文件顶部块注释。
 * 这里只列每字段的快速描述,完整约束以顶部为准。
 */
struct Package {
    uint16_t pk_len;    // 包总长。语义随传输方向变化:
                        //   - client → gateway: header + pk_data, **不含 tail**
                        //     (客户端填写,最大值 = PKG_MAX_LEN - PKG_TAIL_LEN = 65527)
                        //   - gateway → backend: header + pk_data + tail (网关重写)
                        //     (最大值 = PKG_MAX_LEN = 65535)
    uint16_t pk_id;     // 业务消息号。合法范围 [1, MAX_HANDLERS) = [1, 1024)。
                        // 0 或 >= 1024 视为非法,server 端 recv_pk 返 -5。
    uint32_t pk_idem;   // 幂等 ID。**必须 > 0**,客户端 session 内单调递增。
                        // 0 视为无效(recv_pk 返 -6),<= 上次值视为重复(recv_pk 返 0)。
    uint32_t pk_dst_id; // 目标服务类型(路由键)。**必须 > 0**,0 视为非法(recv_pk 返 -7)。
    uint8_t  pk_data[]; // 业务 payload。长度 = pk_len - HEADER [- TAIL, backend 方向]。
};


/**
 * @brief 网关追加的后缀,只在网关 → 业务服 方向上出现。
 *
 * 网关在转发到后端前 stamp 这 8 字节,并**重写 pk_len 把 tail 算进去**。
 * 后端按 pk_len 切完整字节即为 "Package + Tail"; tail 起始 = (buf + pk_len - 8)。
 * pkt_src_id 是网关从 conv→session 查到的 FromPlayerID,**客户端无法伪造**。
 */
struct PackageTail {
    uint32_t pkt_src_id;    // FromPlayerID (网关 stamp, 可信; 0 通常表示未鉴权)
    uint32_t pkt_src_addr;  // 客户端 IPv4 地址 (IPv6 暂不支持; host 序读取后是普通 uint32)
};


#pragma pack(pop)


/**
 * @defgroup byteorder 字节序转换
 *
 * 协议字节序分层：
 *   - KCP 协议层（conv / sn / ts / ...）：**小端序**，由 ikcp.c 决定，对应用层透明
 *   - Package / PackageTail（应用层）：**大端序（网络字节序）**
 *
 * 两层互不影响 —— KCP 只解析自身头部，Package 字节对 KCP 是 opaque payload。
 *
 * 用法：
 *   - 发送前：本地填 host 序 → 调用 pk_hton / pkt_hton
 *   - 接收后：cast 为 Package* / PackageTail* → 调用 pk_ntoh / pkt_ntoh → 读字段
 *
 * @note 在大端机上 htons/htonl 是空操作，跨平台自动正确。
 * @warning 转换为**原地修改**；同一字段重复调用一次会自己还原。
 * @{
 */

/**
 * @brief 本机字节序 → 网络字节序（发送 Package 前调用）
 * @param p 待转换的 Package 头指针
 */
inline void
pk_hton(Package* p) noexcept {
    p->pk_len    = htons(p->pk_len);
    p->pk_id     = htons(p->pk_id);
    p->pk_idem  = htonl(p->pk_idem);
    p->pk_dst_id = htonl(p->pk_dst_id);
}


/**
 * @brief 网络字节序 → 本机字节序（接收 Package 后调用）
 * @param p 待转换的 Package 头指针
 */
inline void
pk_ntoh(Package* p) noexcept {
    p->pk_len    = ntohs(p->pk_len);
    p->pk_id     = ntohs(p->pk_id);
    p->pk_idem  = ntohl(p->pk_idem);
    p->pk_dst_id = ntohl(p->pk_dst_id);
}


/**
 * @brief 本机字节序 → 网络字节序（发送 PackageTail 前调用）
 * @param p 待转换的 PackageTail 指针
 */
inline void
pkt_hton(PackageTail* p) noexcept {
    p->pkt_src_id   = htonl(p->pkt_src_id);
    p->pkt_src_addr = htonl(p->pkt_src_addr);
}


/**
 * @brief 网络字节序 → 本机字节序（接收 PackageTail 后调用）
 * @param p 待转换的 PackageTail 指针
 */
inline void
pkt_ntoh(PackageTail* p) noexcept {
    p->pkt_src_id   = ntohl(p->pkt_src_id);
    p->pkt_src_addr = ntohl(p->pkt_src_addr);
}


// 协议常量。语义见文件顶部"单包上限"注释。
constexpr int PKG_MAX_LEN      = 65535;                                              // wire frame 总长上限(任意方向)
constexpr int PKG_HEADER_LEN   = sizeof(Package);                                    // 12
constexpr int PKG_TAIL_LEN     = sizeof(PackageTail);                                // 8
constexpr int PKG_MAX_DATA_LEN = PKG_MAX_LEN - PKG_HEADER_LEN - PKG_TAIL_LEN;     // 65515,业务 payload 上限

static_assert(PKG_HEADER_LEN == 12, "Package header size changed");
static_assert(PKG_TAIL_LEN   == 8,  "PackageTail size changed");


} // namespace typhon::core


#endif // __TYPHON_PACKAGE_HPP__