#ifndef __TYPHON_CORE_ERROR_HPP__
#define __TYPHON_CORE_ERROR_HPP__


// ---- 成功 ----
#define xOK                 0

// ---- 中间态 (正数, 非错误) ----
#define xAGAIN              1          // 暂无数据 / 无完整包 / connect 进行中 -> 调用方 break
#define xDUP                2          // 幂等重复, 跳过本条 -> 调用方 continue

// ---- 系统错误: 直接 return -errno (-1 ~ -4095), 不另立宏 ----

// ---- 通用 ----
#define xERR                (-10000)   // 泛化失败 (未细分)
#define xERR_PARAM          (-10001)   // 参数非法

// ---- 协议 / 包 (core::Package) ----
#define xERR_PK_LEN         (-10100)   // 包长度非法
#define xERR_PK_ID          (-10101)   // pk_id 非法 (0 或越界)
#define xERR_PK_SEQ        (-10102)   // pk_idem == 0
#define xERR_PK_DST        (-10103)   // pk_dst_id == 0
#define xERR_PK_SRC        (-10104)   // pk_src_id == 0
#define xERR_PK_DEC         (-10105)   // 解密失败
#define xERR_TOKEN_EXP      (-10106)   // token 过期
#define xERR_TOKEN_VER      (-10107)   // token 验签失败
#define xERR_X25519_KX      (-10108)   // x25519 密钥协商失败
#define xERR_NOT_AUTH       (-10109)   // 未认证
#define xERR_TOKEN_CONV     (-10110)   // 不匹配的 conv
#define xERR_TOKEN_USER     (-10111)   // 不匹配的 user_id


// ---- KCP / ikcp 边界 ----
#define xERR_KCP_CONV       (-10200)   // conv 不符 / 包太短   (ikcp_input -1)
#define xERR_KCP_MALFORM    (-10201)   // len 字段畸形         (ikcp_input -2)
#define xERR_KCP_CMD        (-10202)   // 未知 cmd             (ikcp_input -3)
#define xERR_KCP_TOOBIG     (-10203)   // 包过大 / 分片超窗    (ikcp_send -2)
#define xERR_KCP_BUFSMALL   (-10204)   // 接收 buf 太小        (ikcp_recv -3)

// ---- TCP / 后端  (按需补 -10300…) ----
// ---- Conf / 配置  (按需补 -10400…) ----

// ---- BPF 加载器 ----
#define xERR_BPF_OPEN    (-10500)   // bpf_object__open 失败
#define xERR_BPF_RODATA  (-10501)   // .rodata map 找不到 / 超限 / 写入失败
#define xERR_BPF_LOAD    (-10502)   // bpf_object__load 失败
#define xERR_BPF_FIND    (-10503)   // 按名字找 program / map 失败


namespace typhon::core {


// ikcp_recv: >=0 消息长度 / -1 队列空 / -2 分片未集齐 / -3 用户 buf 太小
inline int
from_ikcp_recv(int rc) noexcept {
    if (rc >= 0) {
        return xOK;
    }

    if (rc == -3) {
        return xERR_KCP_BUFSMALL;
    }

    return xAGAIN;
}

// ikcp_input: 
//   0: 成功 
//  -1: conv 不符·太短 
//  -2: len 畸形 
//  -3: 未知 cmd
inline int
from_ikcp_input(int rc) noexcept {
    switch (rc) {
    case  0: 
        return xOK;

    case -1: 
        return xERR_KCP_CONV;

    case -2: 
        return xERR_KCP_MALFORM;

    case -3: 
        return xERR_KCP_CMD;
    
    default: 
        return xERR;
    }
}

// ikcp_send: 
// >= 0:  已发长度(成功)
//   -1: len < 0 参数错 
//   -2: 包过大·分片超窗
inline int
from_ikcp_send(int rc) noexcept {
    if (rc >= 0)  return xOK;
    if (rc == -1) return xERR_PARAM;
    return xERR_KCP_TOOBIG;
}


} // namespace typhon::core;


#endif // __TYPHON_CORE_ERROR_HPP__
