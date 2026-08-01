#ifndef __ADAM_CORE_ERROR_HPP__
#define __ADAM_CORE_ERROR_HPP__


#include <cerrno>


// 约定: 所有返回 int 的函数一律 >= 0 成功, < 0 失败, 调用方只需判 rc < 0.
// 负数空间按来源分段, 段与段之间不重叠, 因此任何一个负值都能反查出它属于哪一类.


// ----------------------------------------------------------------------------
// 系统错误码
// ----------------------------------------------------------------------------
// 直接 return -errno


// ----------------------------------------------------------------------------
// 服务内部错误码
// ----------------------------------------------------------------------------

// ---- 成功 ----
#define xOK                 (0)

// ---- 中间态 ----
// xAGAIN 直接借 -EAGAIN: 它表达的就是"现在没有, 待会再来", 与内核同义.
// 所有消费点都按恒等比较(== xAGAIN), 且排在 rc < 0 判断之前, 所以取负值不影响判死逻辑.
#define xAGAIN              (-EAGAIN)   // 暂无数据 / 无完整包 / connect 进行中 -> 调用方 break

// ---- 通用 ----
#define xERR                (-200)   // 泛化失败 (未细分)
#define xERR_PARAM          (-201)   // 入参非法 / 输出缓冲不足
#define xERR_NO_HANDLER     (-202)   // 该 PID 没有注册处理器
#define xERR_REJECTED       (-203)   // 业务钩子拒绝

// ---- 协议 / 包 (core::Package) ----
#define xERR_PK_LEN         (-300)   // 包长度非法
#define xERR_PK_PID         (-301)   // pk_id 非法 (0 或越界)
#define xERR_PK_DST         (-302)   // pk_dst_id == 0
#define xERR_PK_SRC         (-303)   // pk_src_id == 0
#define xERR_PK_DEC         (-304)   // 解密失败
#define xERR_TOKEN_EXP      (-305)   // token 过期
#define xERR_TOKEN_VER      (-306)   // token 验签失败
#define xERR_X25519_KX      (-307)   // x25519 密钥协商失败
#define xERR_NOT_AUTH       (-308)   // 未认证
#define xERR_TOKEN_CONV     (-309)   // 不匹配的 conv
#define xERR_TOKEN_USER     (-310)   // 不匹配的 user_id
#define xERR_PK_STATE       (-311)   // 该 PID 在当前会话状态下不合法(消息本身没错, 时机不对)
#define xERR_SEALEDBOX      (-312)   // sealedbox 加解密失败

// ---- KCP / ikcp 边界 ----
// 不另立码: Session 直接把 IKCP_ERR_*(两位数)上抛, 与本层三位数不重叠, 无歧义.
// 打日志用 ikcp_error() 取字符串.

// ---- TCP / 后端 ----
#define xERR_TCP_CLOSED     (-500)   // 对端正常关闭连接(recv 返回 0)

// ---- Conf / 配置 ----
#define xERR_CONF_MISSING   (-600)   // 缺必填字段
#define xERR_CONF_VALUE     (-601)   // 字段值非法(空串 / 越界 / 不在枚举内)

// ---- BPF 加载器 ----
#define xERR_BPF_OPEN       (-700)   // bpf_object__open 失败
#define xERR_BPF_RODATA     (-701)   // .rodata map 找不到 / 超限 / 写入失败
#define xERR_BPF_LOAD       (-702)   // bpf_object__load 失败
#define xERR_BPF_FIND       (-703)   // 按名字找 program / map 失败

// ---- etcd / 服务发现 ----
#define xERR_ETCD_JSON      (-800)   // 应答不是合法 JSON
#define xERR_ETCD_RSP       (-801)   // 应答缺必要字段 / 内容非法


// ----------------------------------------------------------------------------
// 服务之间错误码
// ----------------------------------------------------------------------------
// 后端 -> 网关, 走内网, 写在各 *_RSP 的 code 字段里. 跨进程, 改动要同步后端实现.
// 四位数, 千位分组: 1xxx 框架, 2xxx 起业务自定义.
// 0 是唯一的例外 -- 成功恒为 0, 与内部层的 xOK 一致, 调用方只需判 code != 0.

#define SERR_OK             (0)     // 成功
#define SERR_MISMATCH       (1000)  // payload 自报的 conv/ip 与网关填的 meta 不符
#define SERR_CONFLICT       (1001)  // 状态冲突(目标已存在 / 已被占用)
#define SERR_CUSTOM         (2000)  // 业务自定义起点(本身只作分界, 具体码从 +1 开始)


// ----------------------------------------------------------------------------
// public 错误码
// ----------------------------------------------------------------------------
// 框架 -> 客户端, 出公网 -- 老版本客户端还在跑, 含义一经发布不能再改.
// 跨语言, 改动必须同步 C++/C#/Go 三端(C# 侧见 Lilith/Core/Package.cs).
//
// 五位数, 万位分表 / 千位分框架与业务:
//     1xxxx  终端离开码, 走 OFF 与 KIC 的 code 字段
//     2xxxx  请求失败码, 走 PID_TER_ERROR 的 payload
//     x0xxx  框架定义      x1xxx 起  业务自定义(从各表的 _CUSTOM 之后开始)

// ---- 终端离开码(OFF/KIC 携带): 连接为什么结束了 ----
#define PERR_TER_LEAVE          (10000)  // 客户端主动自离(TER_LEA_REQ)
#define PERR_TER_DISCONNECTED   (10001)  // 超时 / 网络断        -> 建议给宽限期
#define PERR_TER_KICKED         (10002)  // 被踢(通用)           -> 建议给宽限期(马上有新会话接管)
#define PERR_TER_PROTO_ERR      (10003)  // 协议违规             -> 立即清
#define PERR_TER_REJECTED       (10004)  // 登记被业务拒绝        -> 立即清
#define PERR_TER_GW_LOST        (10005)  // 网关连接断(后端本地判定, 一批同时消失)
#define PERR_TER_CUSTOM         (11000)  // 业务自定义起点(本身只作分界, 具体码从 +1 开始)

// 业务码: 由路由服务发起的账号级处置, 客户端据此给用户不同提示.
// 与框架码的区别 -- 框架码描述"连接怎么没的", 这些描述"账号被怎么处置了".
#define PERR_TER_TAKEOVER       (PERR_TER_CUSTOM + 1)  // 顶号: 账号在其他设备登录, 可重新登录
#define PERR_TER_BANNED         (PERR_TER_CUSTOM + 2)  // 封号: 账号被封禁, 重登也会被 Eva 拒绝
#define PERR_TER_ADMIN_KICK     (PERR_TER_CUSTOM + 3)  // 管理员踢下线(未封号), 可立即重新登录

// ---- 请求失败码(PID_TER_ERROR 携带): 连接还在, 但这条请求没送到 ----
// 与 1xxxx 的区别 -- 那些说"连接没了", 这些说"这条请求没到".
// 收到 PID_TER_ERROR 不意味着会话结束, 客户端可以重试或提示用户.
#define PERR_REQ_UNREACHABLE    (20000)  // 目标服务不可达(未发现 / 未连接 / 未鉴权) -> 可稍后重试
#define PERR_REQ_NOT_ACCEPT     (20001)  // 目标服务不受理该 PID -> 协议错或版本错位, 重试无用
#define PERR_REQ_CUSTOM         (21000)  // 业务自定义起点(本身只作分界, 具体码从 +1 开始)


namespace adam::core {


/**
 * @brief 把任意一个码翻成可读字符串, 打日志用. 四层共用这一个函数 --
 *        号段互不重叠, 传什么进来它都能认出是哪一层的.
 *
 *     [-99, -12]     ikcp        转给 ikcp_error()
 *     [-999, -200]   服务内部     x*
 *     [1000, 9999]   服务之间     SERR_*
 *     [10000, ...]   public      PERR_*
 *     0              成功        xOK 与 SERR_OK 同值
 *
 * xAGAIN 先按恒等匹配 -- 它取的是 -EAGAIN, 落在 ikcp 的两位数段里.
 * 未命中具体码时按号段兜底, 能区分"业务自定义"和"根本不在任何段里".
 */
const char*
str_error(int ec) noexcept;


} // namespace adam::core


#endif // __ADAM_CORE_ERROR_HPP__
