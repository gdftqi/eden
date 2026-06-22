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
//   - 这些 cmd 在 ikcp_input 里"早分发"绕过 sn 窗口; REGIST 传输层去重 + ikcp_reset(重连)
//   - 保活 / 空闲超时(on_timeout)
//   - 加解密直链 libsodium: 整条消息 ChaCha20-Poly1305 AEAD + crypto_kx 密钥协商
//
// 约定: 注释里标 [XKCP] 的是扩展部分; 其余为原生 KCP。
//=====================================================================

#include <stddef.h>
#include <stdlib.h>
#include <assert.h>


//=====================================================================
// 32BIT INTEGER DEFINITION 
//=====================================================================
#ifndef __INTEGER_32_BITS__
#define __INTEGER_32_BITS__
#if defined(_WIN64) || defined(WIN64) || defined(__amd64__) || \
	defined(__x86_64) || defined(__x86_64__) || defined(_M_IA64) || \
	defined(_M_AMD64)
	typedef unsigned int ISTDUINT32;
	typedef int ISTDINT32;
#elif defined(_WIN32) || defined(WIN32) || defined(__i386__) || \
	defined(__i386) || defined(_M_X86)
	typedef unsigned long ISTDUINT32;
	typedef long ISTDINT32;
#elif defined(__MACOS__)
	typedef UInt32 ISTDUINT32;
	typedef SInt32 ISTDINT32;
#elif defined(__APPLE__) && defined(__MACH__)
	#include <sys/types.h>
	typedef u_int32_t ISTDUINT32;
	typedef int32_t ISTDINT32;
#elif defined(__BEOS__)
	#include <sys/inttypes.h>
	typedef u_int32_t ISTDUINT32;
	typedef int32_t ISTDINT32;
#elif (defined(_MSC_VER) || defined(__BORLANDC__)) && (!defined(__MSDOS__))
	typedef unsigned __int32 ISTDUINT32;
	typedef __int32 ISTDINT32;
#elif defined(__GNUC__)
	#include <stdint.h>
	typedef uint32_t ISTDUINT32;
	typedef int32_t ISTDINT32;
#else 
	typedef unsigned long ISTDUINT32; 
	typedef long ISTDINT32;
#endif
#endif


//=====================================================================
// Integer Definition
//=====================================================================

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


//=====================================================================
// QUEUE DEFINITION                                                  
//=====================================================================
#ifndef __IQUEUE_DEF__
#define __IQUEUE_DEF__

struct IQUEUEHEAD {
	struct IQUEUEHEAD *next, *prev;
};

typedef struct IQUEUEHEAD iqueue_head;


//---------------------------------------------------------------------
// queue init                                                         
//---------------------------------------------------------------------
#define IQUEUE_HEAD_INIT(name) { &(name), &(name) }
#define IQUEUE_HEAD(name) \
	struct IQUEUEHEAD name = IQUEUE_HEAD_INIT(name)

#define IQUEUE_INIT(ptr) ( \
	(ptr)->next = (ptr), (ptr)->prev = (ptr))

#define IOFFSETOF(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)

#define ICONTAINEROF(ptr, type, member) ( \
		(type*)( ((char*)((type*)ptr)) - IOFFSETOF(type, member)) )

#define IQUEUE_ENTRY(ptr, type, member) ICONTAINEROF(ptr, type, member)


//---------------------------------------------------------------------
// queue operation                     
//---------------------------------------------------------------------
#define IQUEUE_ADD(node, head) ( \
	(node)->prev = (head), (node)->next = (head)->next, \
	(head)->next->prev = (node), (head)->next = (node))

#define IQUEUE_ADD_TAIL(node, head) ( \
	(node)->prev = (head)->prev, (node)->next = (head), \
	(head)->prev->next = (node), (head)->prev = (node))

#define IQUEUE_DEL_BETWEEN(p, n) ((n)->prev = (p), (p)->next = (n))

#define IQUEUE_DEL(entry) (\
	(entry)->next->prev = (entry)->prev, \
	(entry)->prev->next = (entry)->next, \
	(entry)->next = 0, (entry)->prev = 0)

#define IQUEUE_DEL_INIT(entry) do { \
	IQUEUE_DEL(entry); IQUEUE_INIT(entry); } while (0)

#define IQUEUE_IS_EMPTY(entry) ((entry) == (entry)->next)

#define iqueue_init		IQUEUE_INIT
#define iqueue_entry	IQUEUE_ENTRY
#define iqueue_add		IQUEUE_ADD
#define iqueue_add_tail	IQUEUE_ADD_TAIL
#define iqueue_del		IQUEUE_DEL
#define iqueue_del_init	IQUEUE_DEL_INIT
#define iqueue_is_empty IQUEUE_IS_EMPTY

#define IQUEUE_FOREACH(iterator, head, TYPE, MEMBER) \
	for ((iterator) = iqueue_entry((head)->next, TYPE, MEMBER); \
		&((iterator)->MEMBER) != (head); \
		(iterator) = iqueue_entry((iterator)->MEMBER.next, TYPE, MEMBER))

#define iqueue_foreach(iterator, head, TYPE, MEMBER) \
	IQUEUE_FOREACH(iterator, head, TYPE, MEMBER)

#define iqueue_foreach_entry(pos, head) \
	for( (pos) = (head)->next; (pos) != (head) ; (pos) = (pos)->next )
	

#define __iqueue_splice(list, head) do {	\
		iqueue_head *first = (list)->next, *last = (list)->prev; \
		iqueue_head *at = (head)->next; \
		(first)->prev = (head), (head)->next = (first);		\
		(last)->next = (at), (at)->prev = (last); }	while (0)

#define iqueue_splice(list, head) do { \
	if (!iqueue_is_empty(list)) __iqueue_splice(list, head); } while (0)

#define iqueue_splice_init(list, head) do {	\
	iqueue_splice(list, head);	iqueue_init(list); } while (0)


#ifdef _MSC_VER
#pragma warning(disable:4311)
#pragma warning(disable:4312)
#pragma warning(disable:4996)
#endif

#endif


//---------------------------------------------------------------------
// BYTE ORDER & ALIGNMENT
//---------------------------------------------------------------------
#ifndef IWORDS_BIG_ENDIAN
    #ifdef _BIG_ENDIAN_
        #if _BIG_ENDIAN_
            #define IWORDS_BIG_ENDIAN 1
        #endif
    #endif
    #ifndef IWORDS_BIG_ENDIAN
        #if defined(__hppa__) || \
            defined(__m68k__) || defined(mc68000) || defined(_M_M68K) || \
            (defined(__MIPS__) && defined(__MIPSEB__)) || \
            defined(__ppc__) || defined(__POWERPC__) || defined(_M_PPC) || \
            defined(__sparc__) || defined(__powerpc__) || \
            defined(__mc68000__) || defined(__s390x__) || defined(__s390__)
            #define IWORDS_BIG_ENDIAN 1
        #endif
    #endif
    #ifndef IWORDS_BIG_ENDIAN
        #define IWORDS_BIG_ENDIAN  0
    #endif
#endif

#ifndef IWORDS_MUST_ALIGN
	#if defined(__i386__) || defined(__i386) || defined(_i386_)
		#define IWORDS_MUST_ALIGN 0
	#elif defined(_M_IX86) || defined(_X86_) || defined(__x86_64__)
		#define IWORDS_MUST_ALIGN 0
	#elif defined(__amd64) || defined(__amd64__)
		#define IWORDS_MUST_ALIGN 0
	#else
		#define IWORDS_MUST_ALIGN 1
	#endif
#endif


//=====================================================================
// Predefine struct
//=====================================================================
struct XKCPCB;
typedef struct XKCPCB xkcpcb;

// REGIST 去重: 握手 id 取 REGIST_REQ payload 头部这么多字节(= 客户端临时公钥, ikcp 当不透明)
#define XKCP_REGIST_ID_LEN 32
// 缓存的 RSP 字节上限(整段), 用于对"重发的 REQ"重发同一份 RSP
#define XKCP_REGIST_RSP_MAX 256


//=====================================================================
// SEGMENT
//=====================================================================
// KCP 段(= 一个 KCP 协议单元, wire 上是 24B 头 + data)。原生结构, 字段含义:
struct XKCPSEG {
	struct IQUEUEHEAD node;   // 链入 snd_queue/snd_buf/rcv_queue/rcv_buf 的节点
	uint32_t conv;             // 会话号(两端必须一致, 否则 ikcp_input 丢弃)
	uint32_t cmd;              // 命令: PUSH/ACK/WASK/WINS, 以及 [XKCP] REGIST/RST/KIC/PING/PONG
	uint32_t frg;              // 分片编号(同一消息倒数第几片, 0 = 最后一片)
	uint32_t wnd;              // 发送者通告的可用接收窗口
	uint32_t ts;               // 发送时间戳(算 RTT 用; 每次重传都会刷新)
	uint32_t sn;               // 段序号
	uint32_t una;              // 发送者的 rcv_nxt(此序号之前的都已收到)
	uint32_t len;              // data 字节数
	uint32_t resendts;         // 下次重传时刻
	uint32_t rto;              // 该段的重传超时
	uint32_t fastack;          // 被跨越次数(达到阈值触发快速重传)
	uint32_t xmit;             // 已发送次数
	uint8_t  data[1];          // 段负载(柔性数组)
};


//---------------------------------------------------------------------
// IKCPOPS - 可插拔拥塞控制策略 [XKCP 扩展, 原生 KCP 没有]
// 经 kcp->ccops 注入; 在 ack/超时/快重传/发包等时机回调给具体拥塞算法。
// 不设置(NULL)则走原生 KCP 的内置拥塞行为。
//---------------------------------------------------------------------
struct XKCPOPS {
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
};


//---------------------------------------------------------------------
// IKCPCB
//---------------------------------------------------------------------
struct XKCPCB {
	// ----- 原生 KCP 状态 -----
	uint32_t  conv;  		// 会话号
	uint32_t  mtu;        	// MTU
	uint32_t  mss;			// 最大分片(mtu-overhead)
	uint32_t  state;   		// 状态(-1=链路死) 
	uint32_t  snd_una; 		// 最早未确认
	uint32_t  snd_nxt; 		// 下一发送序号
	uint32_t  rcv_nxt;    	// 下一期望接收序号
	uint32_t  ts_recent; 	// 时间戳记录
	uint32_t  ts_lastack; 	// 上次 ack 时间
	uint32_t  ssthresh;   	// 慢启动阈值
	int32_t   rx_rttval; 	// RTT 抖动 
	int32_t   rx_srtt; 		// 平滑 RTT
	int32_t   rx_rto; 		// 重传超时 RTO 
	int32_t   rx_minrto; 	// 最小 RTO
	uint32_t  snd_wnd; 		// 发送窗口
	uint32_t  rcv_wnd; 		// 接收窗口
	uint32_t  rmt_wnd; 		// 对端窗口
	uint32_t  cwnd; 			// 拥塞窗口
	uint32_t  probe; 		// 探测标志
	uint32_t  current; 		// 当前时刻
	uint32_t  interval; 		// flush 间隔
	uint32_t  ts_flush; 		// 下次 flush 时刻
	uint32_t  xmit; 			// 总重传次数
	uint32_t  nrcv_buf; 		// rcv_buf
	uint32_t  nsnd_buf;      // snd_buf 内的段数
	uint32_t  nrcv_que; 		// rcv_queue
	uint32_t  nsnd_que;    	// snd_queue
	uint32_t  nodelay; 		// 快速模式开关
	uint32_t  updated;       // 是否已调过 ikcp_update
	uint32_t  ts_probe;
	uint32_t  probe_wait;    // 窗口探测计时
	uint32_t  dead_link; 	// 连续重传多少次判链路死
	uint32_t  incr;          // 拥塞窗口字节增量
	uint32_t* acklist;       // 待回 ACK 列表(sn,ts 成对存)
	uint32_t  ackcount;      // 待回 ACK 个数
	uint32_t  ackblock;      // acklist 容量
	uint32_t  ackedlen;
	void*    user;          // 用户指针(本项目 = kcp::Session)
	uint8_t* buffer;        // flush 组包临时缓冲
	int32_t   fastresend;    // 跨越多少次触发快速重传(0=关)
	int32_t   fastlimit;     // 快速重传的 xmit 次数上限
	int32_t   nocwnd;		// 关闭拥塞控制
	int32_t   stream;        // 流模式
	void*    congest;       // 拥塞策略私有状态
	int32_t   logmask;       // 日志掩码

	struct IQUEUEHEAD snd_queue;  // 待分片发送队列(ikcp_send 入口)
	struct IQUEUEHEAD rcv_queue;  // 已就绪、待上层读取队列(ikcp_recv 出口)
	struct IQUEUEHEAD snd_buf;    // 已发送待确认缓冲(重传的源)
	struct IQUEUEHEAD rcv_buf;    // 乱序到达的暂存缓冲
	struct XKCPOPS*   ccops;      // 可插拔拥塞控制策略(原生无)
	int (*output)(const uint8_t* buf, int len, struct XKCPCB *kcp);
	void (*writelog)(const char *log, struct XKCPCB *kcp, void *user);

	void (*on_regist)(const uint8_t *data, int len, struct XKCPCB *kcp, uint8_t *out_data, int *out_len); // 服务端收 REGIST_REQ(仅新握手)
	void (*on_regist_rsp)(const uint8_t *data, int len, struct XKCPCB *kcp); 							// 客户端收 REGIST_RSP
	void (*on_rst)(const uint8_t *data, int len, struct XKCPCB *kcp);        							// 客户端收 RST(顶号)
	void (*on_kic)(const uint8_t *data, int len, struct XKCPCB *kcp);        							// 客户端收 KIC(被踢)

	// REGIST 传输层去重缓存: 当前握手 id(REQ payload 头部) + 对应 RSP 字节, 供重发 REQ 时重发 RSP
	uint8_t regist_id[XKCP_REGIST_ID_LEN];
	int32_t  has_regist;
	uint8_t regist_rsp[XKCP_REGIST_RSP_MAX];
	int32_t  regist_rsp_len;

	// 保活/超时(ms 用 ikcp 的 current 32bit 时钟; 阈值 0 = 关闭)
	uint32_t last_snd_ms;      // 最近一次发送
	uint32_t last_rcv_ms;      // 最近一次收到
	uint32_t ping_interval;    // 空闲多久发 PING
	uint32_t dead_timeout;     // 多久没收到判死
	int32_t  dead;             // on_timeout 只触发一次
	void (*on_timeout)(struct XKCPCB *kcp);  // 判死回调(上层摘会话)

	// 加解密(直链 libsodium): 会话密钥 + 按消息的 nonce 计数器 + 方向
	uint8_t tx_key[32];     // 发送密钥
	uint8_t rx_key[32];     // 接收密钥
	uint8_t eph_pk[32];     // 握手临时公钥(客户端在 REQ→RSP 之间保留)
	uint8_t eph_sk[32];     // 握手临时私钥
	uint32_t last_snd_seq;         // AEAD 发送 nonce 计数器(按"消息", 非段)
	uint32_t last_rcv_seq;         // AEAD 接收 nonce 计数器
	uint8_t snd_dir;        // 发送方向: 0=C2S 1=S2C
	uint8_t rcv_dir;        // 接收方向
	int32_t has_key;                  // 密钥已就绪(kx 完成); 0 时 send/recv 不加解密
};


#define XKCP_LOG_OUTPUT			1
#define XKCP_LOG_INPUT			2
#define XKCP_LOG_SEND			4
#define XKCP_LOG_RECV			8
#define XKCP_LOG_IN_DATA		16
#define XKCP_LOG_IN_ACK			32
#define XKCP_LOG_IN_PROBE		64
#define XKCP_LOG_IN_WINS		128
#define XKCP_LOG_OUT_DATA		256
#define XKCP_LOG_OUT_ACK		512
#define XKCP_LOG_OUT_PROBE		1024
#define XKCP_LOG_OUT_WINS		2048

#ifdef __cplusplus
extern "C" {
#endif


/**
 * @brief 创建 kcp
 */
xkcpcb*
xkcp_create(uint32_t conv, void *user);


/**
 * @brief 释放 kcp
 */
void
xkcp_release(xkcpcb *kcp);

/**
 * @brief 重置 kcp
 */
void
xkcp_reset(xkcpcb *kcp);


/**
 * @brief 生成 x25519 密钥对
 */
void
xkcp_kx_keygen(xkcpcb *kcp);


/**
 * @brief 服务端: x25519 密钥协商
 * 
 * @param client_pk 客户端公钥
 * 
 * @param out_server_pk 服务端公钥
 */
int
xkcp_kx_server(xkcpcb *kcp, const uint8_t *client_pk, uint8_t *out_server_pk);


/**
 * @brief 客户端: x25519 密钥协商
 * 
 * @param server_pk 服务端公钥
 */
int
xkcp_kx_client(xkcpcb *kcp, const uint8_t *server_pk);                              


/**
 * @brief 接收消息
 */
int
xkcp_recv(xkcpcb *kcp, uint8_t *buffer, int len);


/**
 * @brief 发送消息
 */
int
xkcp_send(xkcpcb *kcp, const uint8_t *buffer, int len);


/**
 * @brief 帧刷新
 */
void
xkcp_update(xkcpcb *kcp, uint32_t current);


/**
 * @brief 计算下一次帧间隔时间(ms)
 */
uint32_t
xkcp_check(const xkcpcb *kcp, uint32_t current);


/**
 * @brief 组包
 */
int
xkcp_input(xkcpcb *kcp, const uint8_t *data, int size);


/**
 * @brief 立即刷新发送缓冲区
 */
void
xkcp_flush(xkcpcb *kcp);


/**
 * @brief 获取可读数据
 * 
 * @return 成功返回消息长度, 否则返回 -1(表示没有准备好的数据)
 */
int
xkcp_peeksize(const xkcpcb *kcp);


/**
 * @brief 设置MTU
 */
int
xkcp_setmtu(xkcpcb *kcp, int mtu);


/** 
 * @brief 设置 发送/接收 窗口
 */
int
xkcp_wndsize(xkcpcb *kcp, int sndwnd, int rcvwnd);


/**
 * @brief 获取待发送的包数量
 */
int
xkcp_waitsnd(const xkcpcb *kcp);


/**
 * @brief 设置 kcp 核心参数
 * 
 * @param nodelay 是否开启低延迟模式, 默认关闭. 1, 极速模式. 2, 将会开启流模式
 * 
 * @param interval 帧间隔(ms)
 * 
 * @param resend 快速重传值
 * 
 * @param nc 是否关闭拥塞控制算法. 0: 默认开启. 1, 关闭拥塞控制算法
 */
int
xkcp_nodelay(xkcpcb *kcp, int nodelay, int interval, int resend, int nc);


/**
 * @brief 设置拥塞控制算法
 */
int
xkcp_setcc(xkcpcb *kcp, const struct XKCPOPS *ops);

// write log with kcp->writelog
void
xkcp_log(xkcpcb *kcp, int mask, const char *fmt, ...);


/**
 * @brief 从原始数据获取 conv
 */
uint32_t
xkcp_getconv(const void *ptr);


#ifdef __cplusplus
}
#endif

#endif