//=====================================================================
//
// KCP - A Better ARQ Protocol Implementation
// skywind3000 (at) gmail.com, 2010-2011
//  
// Features:
// + Average RTT reduce 30% - 40% vs traditional ARQ like tcp.
// + Maximum RTT reduce three times vs tcp.
// + Lightweight, distributed as a single source file.
//
//=====================================================================
#ifndef _XKCP_H_
#define _XKCP_H_

//=====================================================================
// XKCP —— 基于 KCP(skywind3000)的 fork 扩展协议
//
// 原生 KCP 只做可靠传输(ARQ / 收发窗口 / 拥塞 / 分片重组)。XKCP 在其上把
// "连接管理 + 安全 + 保活"做进协议层, 让上层(kcp::Server/Session)只处理业务。
//
// 扩展点(完整说明见仓库根 XKCP.md):
//   - 控制 cmd 85–90: REGIST_REQ/RSP(握手)、RST(顶号)、KIC(踢人)、PING/PONG(保活)
//   - 这些 cmd 在 xkcp_input 里"早分发"绕过 sn 窗口; REGIST 传输层去重 + xkcp_reset(重连)
//   - 加解密直链 libsodium: 整条消息 ChaCha20-Poly1305 AEAD + crypto_kx 密钥协商
//
// 约定: 注释里标 [XKCP] 的是扩展部分; 其余为原生 KCP。
//=====================================================================
#include <stddef.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>


#ifndef INLINE
#if defined(__GNUC__)

#if (__GNUC__ > 3) || ((__GNUC__ == 3) && (__GNUC_MINOR__ >= 1))
#define INLINE         __inline__ __attribute__((always_inline))
#else
#define INLINE         __inline__
#endif

#elif (defined(_MSC_VER) || defined(__BORLANDC__) || defined(__WATCOMC__))
#define INLINE __inline
#else
#define INLINE 
#endif
#endif

#if (!defined(__cplusplus)) && (!defined(inline))
#define inline INLINE
#endif


#ifndef __XQUEUE_DEF__
#define __XQUEUE_DEF__


typedef struct XQUEUEHEAD {
    struct XQUEUEHEAD *next, *prev;
} iqueue_head;


#define XQUEUE_HEAD_INIT(name) { &(name), &(name) }
#define IQUEUE_HEAD(name) \
    struct IQUEUEHEAD name = XQUEUE_HEAD_INIT(name)

#define IQUEUE_INIT(ptr) ( \
    (ptr)->next = (ptr), (ptr)->prev = (ptr))

#define XOFFSETOF(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)

#define XCONTAINEROF(ptr, type, member) ( \
        (type*)( ((char*)((type*)ptr)) - XOFFSETOF(type, member)) )

#define XQUEUE_ENTRY(ptr, type, member) XCONTAINEROF(ptr, type, member)


//---------------------------------------------------------------------
// queue operation                     
//---------------------------------------------------------------------
#define XQUEUE_ADD(node, head) ( \
    (node)->prev = (head), (node)->next = (head)->next, \
    (head)->next->prev = (node), (head)->next = (node))

#define XQUEUE_ADD_TAIL(node, head) ( \
    (node)->prev = (head)->prev, (node)->next = (head), \
    (head)->prev->next = (node), (head)->prev = (node))

#define XQUEUE_DEL_BETWEEN(p, n) ((n)->prev = (p), (p)->next = (n))

#define XQUEUE_DEL(entry) (\
    (entry)->next->prev = (entry)->prev, \
    (entry)->prev->next = (entry)->next, \
    (entry)->next = 0, (entry)->prev = 0)

#define XQUEUE_DEL_INIT(entry) do { \
    XQUEUE_DEL(entry); IQUEUE_INIT(entry); } while (0)

#define XQUEUE_IS_EMPTY(entry) ((entry) == (entry)->next)

#define xqueue_init     IQUEUE_INIT
#define xqueue_entry    XQUEUE_ENTRY
#define xqueue_add      XQUEUE_ADD
#define xqueue_add_tail XQUEUE_ADD_TAIL
#define xqueue_del      XQUEUE_DEL
#define xqueue_del_init XQUEUE_DEL_INIT
#define xqueue_is_empty IQUEUE_IS_EMPTY

#define XQUEUE_FOREACH(iterator, head, TYPE, MEMBER) \
    for ((iterator) = xqueue_entry((head)->next, TYPE, MEMBER); \
        &((iterator)->MEMBER) != (head); \
        (iterator) = xqueue_entry((iterator)->MEMBER.next, TYPE, MEMBER))

#define xqueue_foreach(iterator, head, TYPE, MEMBER) \
    XQUEUE_FOREACH(iterator, head, TYPE, MEMBER)

#define xqueue_foreach_entry(pos, head) \
    for( (pos) = (head)->next; (pos) != (head) ; (pos) = (pos)->next )
    

#define __xqueue_splice(list, head) do {    \
        iqueue_head *first = (list)->next, *last = (list)->prev; \
        iqueue_head *at = (head)->next; \
        (first)->prev = (head), (head)->next = (first);     \
        (last)->next = (at), (at)->prev = (last); } while (0)

#define xqueue_splice(list, head) do { \
    if (!xqueue_is_empty(list)) __xqueue_splice(list, head); } while (0)

#define xqueue_splice_init(list, head) do { \
    xqueue_splice(list, head);  xqueue_init(list); } while (0)


#ifdef _MSC_VER
#pragma warning(disable:4311)
#pragma warning(disable:4312)
#pragma warning(disable:4996)
#endif

#endif


//---------------------------------------------------------------------
// BYTE ORDER & ALIGNMENT
//---------------------------------------------------------------------
#ifndef XWORDS_BIG_ENDIAN
    #ifdef _BIG_ENDIAN_
        #if _BIG_ENDIAN_
            #define XWORDS_BIG_ENDIAN 1
        #endif
    #endif
    #ifndef XWORDS_BIG_ENDIAN
        #if defined(__hppa__) || \
            defined(__m68k__) || defined(mc68000) || defined(_M_M68K) || \
            (defined(__MIPS__) && defined(__MIPSEB__)) || \
            defined(__ppc__) || defined(__POWERPC__) || defined(_M_PPC) || \
            defined(__sparc__) || defined(__powerpc__) || \
            defined(__mc68000__) || defined(__s390x__) || defined(__s390__)
            #define XWORDS_BIG_ENDIAN 1
        #endif
    #endif
    #ifndef XWORDS_BIG_ENDIAN
        #define XWORDS_BIG_ENDIAN  0
    #endif
#endif

#ifndef XWORDS_MUST_ALIGN
    #if defined(__i386__) || defined(__i386) || defined(_i386_)
        #define XWORDS_MUST_ALIGN 0
    #elif defined(_M_IX86) || defined(_X86_) || defined(__x86_64__)
        #define XWORDS_MUST_ALIGN 0
    #elif defined(__amd64) || defined(__amd64__)
        #define XWORDS_MUST_ALIGN 0
    #else
        #define XWORDS_MUST_ALIGN 1
    #endif
#endif


struct XKCPCB;
typedef struct XKCPCB xkcpcb;


/**
 * @brief kcp 段
 */
typedef struct XKCPSEG {
    struct XQUEUEHEAD node;    // 链入 snd_queue/snd_buf/rcv_queue/rcv_buf 的节点
    uint32_t conv;             // [N:32] 会话号(两端必须一致, 否则 xkcp_input 丢弃)
    uint32_t cmd;              // [N: 8] 命令: PUSH/ACK/WASK/WINS, 以及 [XKCP] REGIST/RST/KIC/PING/PONG
    uint32_t frg;              // [N: 8] 分片编号(同一消息倒数第几片, 0 = 最后一片)
    uint32_t wnd;              // [N:16] 发送者通告的可用接收窗口
    uint32_t ts;               // [N:32] 发送时间戳(算 RTT 用; 每次重传都会刷新)
    uint32_t sn;               // [N:32] 段序号
    uint32_t una;              // [N:32] 发送者的 rcv_nxt(此序号之前的都已收到)
    uint32_t len;              // [N:32] data 字节数
    uint32_t resendts;         // 下次重传时刻
    uint32_t rto;              // 该段的重传超时
    uint32_t fastack;          // 被跨越次数(达到阈值触发快速重传)
    uint32_t xmit;             // 已发送次数
    uint8_t  data[1];          // 段负载(柔性数组)
} xkcpseg;


typedef struct XKCPOPS {
    const char *name;
    int  (*init)(xkcpcb *kcp);
    void (*release)(xkcpcb *kcp);
    void (*on_ack)(xkcpcb *kcp, uint32_t acked_segs, uint32_t acked_bytes, uint32_t prior_in_flight);
    void (*on_fast_retransmit)(xkcpcb *kcp, uint32_t fast_retrans, uint32_t inflight, uint32_t prior_cwnd);
    void (*on_timeout)(xkcpcb *kcp, uint32_t prior_cwnd);
    void (*on_tick)(xkcpcb *kcp);
    void (*on_app_limited)(xkcpcb *kcp, uint32_t inflight);
    void (*on_rtt)(xkcpcb *kcp, int32_t rtt);
    void (*on_pkt_sent)(xkcpcb *kcp, uint32_t sn, uint32_t ts, uint32_t len, uint32_t inflight, uint32_t xmit);
    void (*on_pkt_acked)(xkcpcb *kcp, uint32_t sn, uint32_t ts, uint32_t len, int32_t rtt, uint32_t xmit);
    uint32_t (*get_info)(xkcpcb *kcp, void *buf, uint32_t bufsize);
    uint32_t (*pacing_rate)(xkcpcb *kcp);
} xkcpops;


typedef struct XKCPCB {
    // ===== 标识 / 基本配置 =====
    uint32_t  conv;          // 会话号(两端一致; 本项目 = userID 路由键)
    uint32_t  state;         // 链路状态(-1 = 链路死)

    // ===== 收发序号 =====
    uint32_t  snd_una;       // 最早未确认序号
    uint32_t  snd_nxt;       // 下一发送序号
    uint32_t  rcv_nxt;       // 下一期望接收序号

    // ===== 窗口 / 拥塞 =====
    uint32_t  rmt_wnd;       // 对端通告窗口
    uint32_t  cwnd;          // 拥塞窗口
    uint32_t  ssthresh;      // 慢启动阈值
    uint32_t  incr;          // 拥塞窗口字节增量
    uint32_t  probe;         // 窗口探测标志(ASK_SEND / ASK_TELL)
    uint32_t  probe_wait;    // 窗口探测计时
    uint32_t  ts_probe;      // 下次窗口探测时刻

    // ===== RTT / 计时 / 重传 =====
    int32_t   rx_rttval;     // RTT 抖动
    int32_t   rx_srtt;       // 平滑 RTT
    int32_t   rx_rto;        // 重传超时 RTO
    uint32_t  current;       // 当前时刻(ms, 由 xkcp_update 传入)
    uint32_t  ts_flush;      // 下次 flush 时刻
    uint32_t  updated;       // 是否已调过 xkcp_update
    uint32_t  xmit;          // 总重传次数

    // ===== 队列 / 缓冲 / 计数 =====
    struct XQUEUEHEAD snd_queue;   // 待分片发送队列(xkcp_send 入口)
    struct XQUEUEHEAD rcv_queue;   // 已就绪待读队列(xkcp_recv 出口)
    struct XQUEUEHEAD snd_buf;     // 已发送待确认缓冲(重传源)
    struct XQUEUEHEAD rcv_buf;     // 乱序到达暂存缓冲
    uint32_t  nsnd_que;            // snd_queue 段数
    uint32_t  nrcv_que;            // rcv_queue 段数
    uint32_t  nsnd_buf;            // snd_buf 段数
    uint32_t  nrcv_buf;            // rcv_buf 段数
    uint8_t*  buffer;              // flush 组包临时缓冲

    // ===== 待回 ACK 列表 =====
    uint32_t* acklist;       // 待回 ACK 列表(sn,ts 成对存)
    uint32_t  ackcount;      // 待回 ACK 个数
    uint32_t  ackblock;      // acklist 容量
    uint32_t  ackedlen;      // 本次 input 累计被确认字节数

    
    uint8_t*  mac_buf;     // [X] 出向信封暂存 [8B SipHash MAC | buffer]
    int32_t   auth;        // [X] 是否已缓存握手

    // [X] 保活 / 空闲超时(用 current 这个 32bit 时钟; 阈值 0 = 关闭)
    uint32_t  last_snd_ms;   // [X] 最近一次发送时刻
    uint32_t  last_rcv_ms;   // [X] 最近一次收到时刻
    uint32_t  gen;           // [X] 世代号

    // [X] 加解密(直链 libsodium): 会话密钥 + 按消息 nonce 计数器 + 方向
    uint8_t   tx_key[32];    // [X] 发送密钥
    uint8_t   rx_key[32];    // [X] 接收密钥
    uint8_t   eph_pk[32];    // [X] 握手临时公钥
    uint8_t   eph_sk[32];    // [X] 握手临时私钥
    uint32_t  snd_seq;       // [X] AEAD 发送 nonce 计数器(按消息)
    uint32_t  rcv_seq;       // [X] AEAD 接收 nonce 计数器(按消息)
    uint8_t   snd_dir;       // [X] 发送方向(0 = C2S, 1 = S2C)
    uint8_t   rcv_dir;       // [X] 接收方向

    // ===== 用户指针 / 回调 =====
    void*  user;          // 用户指针(本项目 = kcp::Session)
    int  (*output)(const uint8_t* buf, int len, struct XKCPCB *kcp);    // 出向发送回调
    void (*writelog)(const char *log, struct XKCPCB *kcp, void *user);  // 日志回调
} xkcpcb;


typedef struct XKCPTOKEN {
    uint64_t expire;     // 过期时间
    uint32_t conv;       // conv
    uint32_t gen;        // 世代号
    uint8_t  peer_pk[32];// 对端的 x25519 公钥
    uint8_t  sign[64];   // ed25519 签名
} xkcptoken;


#define XKCP_LOG_OUTPUT         1
#define XKCP_LOG_INPUT          2
#define XKCP_LOG_SEND           4
#define XKCP_LOG_RECV           8
#define XKCP_LOG_IN_DATA        16
#define XKCP_LOG_IN_ACK         32
#define XKCP_LOG_IN_PROBE       64
#define XKCP_LOG_IN_WINS        128
#define XKCP_LOG_OUT_DATA       256
#define XKCP_LOG_OUT_ACK        512
#define XKCP_LOG_OUT_PROBE      1024
#define XKCP_LOG_OUT_WINS       2048


#define XKCP_OK    (0)
#define XKCP_ERR   (-1)
#define XKCP_AGAIN (-EAGAIN)


#ifdef __cplusplus
extern "C" {
#endif


/**
 * @brief 生成本端握手用的 x25519 临时密钥对(存入 kcp->eph_pk / eph_sk)
 * @param kcp  会话
 */
void
xkcp_x25519_keygen(xkcpcb* kcp);


/**
 * @brief 服务端侧 x25519 密钥协商; 方向 snd=S2C / rcv=C2S, nonce seq 归零
 * @param kcp            会话
 * @param client_pk      对端(客户端)公钥
 * @param out_server_pk  [out] 本端新生成的服务端公钥(需回传给客户端)
 * @return 成功 0; 失败 -1
 */
int
xkcp_kx_server(xkcpcb *kcp, const uint8_t *client_pk);


/**
 * @brief 客户端侧 x25519 密钥协商; 用本端 eph 私钥与服务端公钥导出会话密钥
 * @param kcp        会话
 * @param server_pk  对端(服务端)公钥
 * @return 成功 0; 失败 -1
 */
int
xkcp_kx_client(xkcpcb *kcp, const uint8_t *server_pk);


/**
 * @brief [XKCP] 初始化全局配置(鉴权密钥 + KCP 调参), 进程启动时调用一次, 所有会话共享
 * @note  各调参传 0 用内置默认值, 非 0 用传入值; 密钥定长拷入
 */
void
xkcp_init(
    const uint8_t* x25519_pk,   // 用于 sealedbox 加解密的公钥
    const uint8_t* x25519_sk,   // 用于 sealedbox 加解密的私钥
    const uint8_t* ed25519_pk,  // 用于 ed25519 验签的公钥
    const uint8_t* siphash_key, // 用于 envelope_mac 验签的密钥
    uint32_t snd_wnd,           // 发送窗口(段)
    uint32_t rcv_wnd,           // 接收窗口(段)
    uint32_t interval,          // flush 间隔(ms)
    int32_t  fastresend,        // 快速重传
    int32_t  fastlimit,         // 快速重传的 xmit 次数上限
    uint32_t dead_link,         // 连续重传多少次判链路死
    uint32_t dead_timeout       // 多久没收到判死(ms)
);


/**
 * @brief 创建一个 kcp 会话控制块(xkcpcb)
 * @param conv  会话号, 两端必须一致(本项目 = userID 路由键)
 * @param user  用户指针, 原样回传给 output/writelog 回调(本项目 = kcp::Session)
 * @return 成功返回 xkcpcb*; 内存分配失败返回 NULL
 */
xkcpcb*
xkcp_create(uint32_t conv, void *user);


/**
 * @brief 释放 kcp 会话(收发队列 / acklist / buffer / mac_buf 全部回收)
 * @param kcp  由 xkcp_create 创建的会话
 */
void
xkcp_release(xkcpcb *kcp);

/**
 * @brief 复位传输状态用于重连: 保留 conv 与配置, 清空收发队列、序号归零
 * @param kcp  会话
 */
void
xkcp_reset(xkcpcb *kcp);


int
xkcp_sync(xkcpcb *kcp, const uint8_t* token, int len);


/**
 * @brief 喂入一个收到的 UDP 数据报(含前置 8B SipHash 信封, 函数内跳过该 8B 不校验)
 * @param kcp   会话
 * @param data  数据报首地址(从 8B 信封开始)
 * @param size  数据报总长度
 * @return 成功 0; -1 长度不足(< 8+24)或 conv 不符; -2 段长越界; -3 未知 cmd
 */
int
xkcp_input(xkcpcb *kcp, const uint8_t *data, int size);


/**
 * @brief 取出一条完整消息到 buffer(authed 后整条 AEAD 解密并剥掉 16B tag)
 * @param kcp     会话
 * @param buffer  [out] 接收缓冲
 * @param len     buffer 容量
 * @return 成功返回消息长度; -1 无数据; -2 消息未收全; -3 buffer 太小; -4 解密/认证失败
 */
int
xkcp_recv(xkcpcb *kcp, uint8_t *buffer, int len);


/**
 * @brief 发送一条消息(authed 后整条 ChaCha20-Poly1305 AEAD 加密, 再分片入发送队列)
 * @param kcp     会话
 * @param buffer  待发送数据
 * @param len     数据长度
 * @return 成功 >=0; 失败 <0(-1 参数/内存/加密错; -2 分片数超过接收窗口上限)
 */
int
xkcp_send(xkcpcb *kcp, const uint8_t *buffer, int len);


/**
 * @brief 帧驱动: 按 interval 触发 flush, 并处理保活(空闲发 PING)与判死
 * @param kcp      会话
 * @param current  当前时刻(ms)
 * @return 正常 0; 判死(超 dead_timeout 未收到)返回 -1(只触发一次, 上层据此摘除会话)
 */
int
xkcp_update(xkcpcb *kcp, uint32_t current);


/**
 * @brief 计算下次应调用 xkcp_update 的时刻(用于 epoll 式定时, 避免空转 tick)
 * @param kcp      会话
 * @param current  当前时刻(ms)
 * @return 下次应调用 xkcp_update 的绝对时刻(ms)
 */
uint32_t
xkcp_check(const xkcpcb *kcp, uint32_t current);


/**
 * @brief 立即把待发送数据 / ACK / 窗口探测刷出(经 output 回调发送)
 * @param kcp  会话
 */
void
xkcp_flush(xkcpcb *kcp);


/**
 * @brief 查看 rcv_queue 队首一条完整消息的长度(不取出)
 * @param kcp  会话
 * @return 成功返回消息长度; -1 没有就绪的完整消息
 */
int
xkcp_peeksize(const xkcpcb *kcp);


/**
 * @brief 获取待发送 + 已发送未确认的段数量(背压判断用)
 * @param kcp  会话
 * @return snd_queue + snd_buf 中的段数
 */
int
xkcp_waitsnd(const xkcpcb *kcp);


/**
 * @brief 设置可插拔拥塞控制策略(传 NULL 用内置算法)
 * @param kcp  会话
 * @param ops  拥塞控制回调集(XKCPOPS)
 * @return 成功 0; 失败 <0
 */
int
xkcp_setcc(xkcpcb *kcp, const struct XKCPOPS *ops);

/**
 * @brief 经 kcp->writelog 输出一条日志(受 kcp->logmask 过滤)
 * @param kcp   会话
 * @param mask  日志类别(XKCP_LOG_*)
 * @param fmt   printf 风格格式串, 后接可变参数
 */
void
xkcp_log(xkcpcb *kcp, int mask, const char *fmt, ...);


/**
 * @brief 从数据报读出 conv(读首部 4 字节)
 * @param ptr  KCP 段首地址; 若传入的是含信封的原始 UDP 数据报, 需 +8 跳过 SipHash 信封
 * @return conv 会话号
 */
uint32_t
xkcp_getconv(const void *ptr);


#ifdef __cplusplus
}
#endif

#endif