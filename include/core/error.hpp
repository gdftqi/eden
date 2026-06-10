#ifndef __TYPHON_CORE_ERROR_HPP__
#define __TYPHON_CORE_ERROR_HPP__


// 全工程统一返回码约定:
//   rc == 0          成功 (xOK)
//   rc  > 0          非致命中间态 (xAGAIN/xDUP),调用方据值决定 break/continue
//   rc  < 0          错误:
//                      -1 ~ -4095   系统错误,值为 -errno (直接 return -errno,不另立宏)
//                      <= -10000    本工程自定义码 (避开 errno 区间)
//
// 判别: rc < 0 即错误; -rc 落在 errno 区间(< 4096)就是系统错误(strerror(-rc)),
//       否则对照下面的 x* 宏。


// ---- 成功 ----
#define xOK                 0

// ---- 中间态 (正数, 非错误) ----
#define xAGAIN              1          // 暂无数据 / 无完整包 / connect 进行中 → 调用方 break
#define xDUP                2          // 幂等重复, 跳过本条 → 调用方 continue

// ---- 系统错误: 直接 return -errno (-1 ~ -4095), 不另立宏 ----

// ---- 通用 ----
#define xERR                (-10000)   // 泛化失败 (未细分)
#define xERR_PARAM          (-10001)   // 参数非法

// ---- 协议 / 包 (core::Package) ----
#define xERR_PKT_LEN        (-10100)   // 包长度非法
#define xERR_PKT_ID         (-10101)   // pk_id 非法 (0 或越界)
#define xERR_PKT_IDEM       (-10102)   // pk_idem == 0
#define xERR_PKT_DST        (-10103)   // pk_dst_id == 0
#define xERR_PKT_DEC        (-10104)   // 解密失败

// ---- KCP / ikcp 边界 ----
#define xERR_KCP_CONV       (-10200)   // conv 不符 / 包太短   (ikcp_input -1)
#define xERR_KCP_MALFORM    (-10201)   // len 字段畸形         (ikcp_input -2)
#define xERR_KCP_CMD        (-10202)   // 未知 cmd             (ikcp_input -3)
#define xERR_KCP_TOOBIG     (-10203)   // 包过大 / 分片超窗    (ikcp_send -2)
#define xERR_KCP_BUFSMALL   (-10204)   // 接收 buf 太小        (ikcp_recv -3)

// ---- TCP / 后端  (按需补 -10300…) ----
// ---- Conf / 配置  (按需补 -10400…) ----
// ---- BPF          (按需补 -10500…) ----


namespace typhon::core {


// ikcp 返回码 → 工程码: 把第三方私有码挡在 Session 边界, 上层只见 x*。
// ikcp 升级若改返回码, 只动下面三个映射即可, 上层不受影响。

// ikcp_recv: >=0 消息长度 / -1 队列空 / -2 分片未集齐 / -3 用户 buf 太小
inline int
from_ikcp_recv(int rc) noexcept {
    if (rc >= 0)  return xOK;              // 成功, 长度由调用方从出参拿
    if (rc == -3) return xERR_KCP_BUFSMALL;
    return xAGAIN;                         // -1 / -2: 没有完整包了, 正常中间态
}

// ikcp_input: 0 成功 / -1 conv 不符·太短 / -2 len 畸形 / -3 未知 cmd
inline int
from_ikcp_input(int rc) noexcept {
    switch (rc) {
    case  0: return xOK;
    case -1: return xERR_KCP_CONV;
    case -2: return xERR_KCP_MALFORM;
    case -3: return xERR_KCP_CMD;
    default: return xERR;
    }
}

// ikcp_send: >=0 已发长度(成功) / -1 len<0 参数错 / -2 包过大·分片超窗
inline int
from_ikcp_send(int rc) noexcept {
    if (rc >= 0)  return xOK;
    if (rc == -1) return xERR_PARAM;
    return xERR_KCP_TOOBIG;
}


} // namespace typhon::core;


#endif // __TYPHON_CORE_ERROR_HPP__
