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
// [XKCP] 本文件是 KCP 的 fork: 原生 ARQ + typhon 扩展(握手 REGIST/复位 RST/踢人 KIC/
// 保活 PING-PONG / 整条消息 AEAD 加解密)。注释里标 [XKCP] 的是扩展, 其余为原生 KCP 算法。
// 总览见 include/kcp/xkcp.h 顶部与仓库根 XKCP.md。
#include "kcp/xkcp.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <sodium.h>
#include <mimalloc-3.2/mimalloc.h>

#define XKCP_FASTACK_CON2SERVE

//=====================================================================
// KCP BASIC
//=====================================================================
#define XKCP_CMD_PUSH       (81)		// cmd: push data
#define XKCP_CMD_ACK        (82)		// cmd: ack
#define XKCP_CMD_WASK       (83)		// cmd: window probe (ask)
#define XKCP_CMD_WINS       (84)		// cmd: window size (tell)
#define XKCP_CMD_REGIST_REQ (85)		// [XKCP] 注册/重连握手请求 (绕过 sn 窗口, 客户端发)
#define XKCP_CMD_REGIST_RSP (86)		// [XKCP] 注册/重连握手应答 (服务端发, 客户端 on_regist_rsp)
#define XKCP_CMD_RST        (87)		// [XKCP] 顶号复位 (其他设备登录, 服务端发给旧端)
#define XKCP_CMD_KIC        (88)		// [XKCP] 主动踢人 (admin, 服务端发)
#define XKCP_CMD_PING       (89)		// [XKCP] 保活心跳 (空闲自动发)
#define XKCP_CMD_PONG       (90)		// [XKCP] 心跳应答
#define XKCP_RTO_NDL  		(30)		// no delay min rto
#define XKCP_RTO_MIN  		(100)		// normal min rto
#define XKCP_RTO_DEF  		(200)
#define XKCP_RTO_MAX  		(60000)
#define XKCP_ASK_SEND  		(1)		// need to send IKCP_CMD_WASK
#define XKCP_ASK_TELL  		(2)		// need to send IKCP_CMD_WINS
#define XKCP_WND_SND   		(32)
#define XKCP_WND_RCV   		(128)       // must >= max fragment size
#define XKCP_MTU_DEF   		(1400)
#define XKCP_ACK_FAST	 	(3)
#define XKCP_INTERVAL	 	(100)
#define XKCP_OVERHEAD  		(24)
#define XKCP_DEADLINK  		(20)
#define XKCP_THRESH_INIT  	(2)
#define XKCP_THRESH_MIN   	(2)
#define XKCP_PROBE_INIT   	(5000)	// 7 secs to probe window size
#define XKCP_PROBE_LIMIT  	(120000)	// up to 120 secs to probe window
#define XKCP_FASTACK_LIMIT 	(5)		// max times to trigger fastack


//---------------------------------------------------------------------
// encode / decode
//---------------------------------------------------------------------

/* encode 8 bits unsigned int */
static inline uint8_t*
xkcp_encode8u(uint8_t *p, uint8_t c) {
	*(uint8_t*)p++ = c;
	return p;
}

/* decode 8 bits unsigned int */
static inline const uint8_t*
xkcp_decode8u(const uint8_t *p, uint8_t *c) {
	*c = *(uint8_t*)p++;
	return p;
}

/* encode 16 bits unsigned int (lsb) */
static inline uint8_t*
xkcp_encode16u(uint8_t *p, uint16_t w) {
#if XWORDS_BIG_ENDIAN || XWORDS_MUST_ALIGN
	*(uint8_t*)(p + 0) = (w & 255);
	*(uint8_t*)(p + 1) = (w >> 8);
#else
	memcpy(p, &w, 2);
#endif
	p += 2;
	return p;
}

/* decode 16 bits unsigned int (lsb) */
static inline const uint8_t*
xkcp_decode16u(const uint8_t *p, uint16_t *w) {
#if XWORDS_BIG_ENDIAN || XWORDS_MUST_ALIGN
	*w = *(const uint8_t*)(p + 1);
	*w = *(const uint8_t*)(p + 0) + (*w << 8);
#else
	memcpy(w, p, 2);
#endif
	p += 2;
	return p;
}

/* encode 32 bits unsigned int (lsb) */
static inline uint8_t*
xkcp_encode32u(uint8_t *p, uint32_t l) {
#if XWORDS_BIG_ENDIAN || XWORDS_MUST_ALIGN
	*(uint8_t*)(p + 0) = (uint8_t)((l >>  0) & 0xff);
	*(uint8_t*)(p + 1) = (uint8_t)((l >>  8) & 0xff);
	*(uint8_t*)(p + 2) = (uint8_t)((l >> 16) & 0xff);
	*(uint8_t*)(p + 3) = (uint8_t)((l >> 24) & 0xff);
#else
	memcpy(p, &l, 4);
#endif
	p += 4;
	return p;
}

/* decode 32 bits unsigned int (lsb) */
static inline const uint8_t*
xkcp_decode32u(const uint8_t *p, uint32_t *l) {
#if XWORDS_BIG_ENDIAN || XWORDS_MUST_ALIGN
	*l = *(const uint8_t*)(p + 3);
	*l = *(const uint8_t*)(p + 2) + (*l << 8);
	*l = *(const uint8_t*)(p + 1) + (*l << 8);
	*l = *(const uint8_t*)(p + 0) + (*l << 8);
#else 
	memcpy(l, p, 4);
#endif
	p += 4;
	return p;
}

static inline uint32_t
_xmin_(uint32_t a, uint32_t b) {
	return a <= b ? a : b;
}

static inline uint32_t
_xmax_(uint32_t a, uint32_t b) {
	return a >= b ? a : b;
}

static inline uint32_t 
_xbound_(uint32_t lower, uint32_t middle, uint32_t upper) {
	return _xmin_(_xmax_(lower, middle), upper);
}

static inline long 
_xtimediff(uint32_t later, uint32_t earlier) {
	return ((int32_t)(later - earlier));
}

//---------------------------------------------------------------------
// manage segment
//---------------------------------------------------------------------
typedef struct XKCPSEG XKCPSEG;


// allocate a new kcp segment
static inline XKCPSEG*
xkcp_segment_new(xkcpcb*, int size) {
	return (XKCPSEG*)mi_malloc(sizeof(XKCPSEG) + size);
}

// delete a segment
static inline void
xkcp_segment_delete(xkcpcb*, XKCPSEG *seg) {
	mi_free(seg);
}

// write log
void
xkcp_log(xkcpcb *kcp, int mask, const char *fmt, ...) {
	char buffer[1024];
	va_list argptr;
	if ((mask & kcp->logmask) == 0 || kcp->writelog == 0) return;
	va_start(argptr, fmt);
	vsprintf(buffer, fmt, argptr);
	va_end(argptr);
	kcp->writelog(buffer, kcp, kcp->user);
}

// check log mask
static int
xkcp_canlog(const xkcpcb *kcp, int mask) {
	if ((mask & kcp->logmask) == 0 || kcp->writelog == NULL) return 0;
	return 1;
}

// output segment
static int
ikcp_output(xkcpcb *kcp, const uint8_t *data, int size) {
	assert(kcp);
	assert(kcp->output);
	if (xkcp_canlog(kcp, XKCP_LOG_OUTPUT)) {
		xkcp_log(kcp, XKCP_LOG_OUTPUT, "[RO] %ld bytes", (long)size);
	}

	if (size == 0) {
		return 0;
	}

	kcp->last_snd_ms = kcp->current;   // 保活: 记录最近一次发送
	return kcp->output((const uint8_t*)data, size, kcp);
}


// [XKCP] 发一个无 payload 的控制段(PING/PONG), 走 output(信封由 output 回调加)
static void 
xkcp_output_ctrl(xkcpcb *kcp, uint32_t cmd) {
	uint8_t buf[XKCP_OVERHEAD];
	uint8_t *p = buf;
	p = xkcp_encode32u(p, kcp->conv);
	p = xkcp_encode8u(p, (uint8_t)cmd);
	p = xkcp_encode8u(p, 0);                       // frg
	p = xkcp_encode16u(p, (uint16_t)kcp->rcv_wnd);  // wnd
	p = xkcp_encode32u(p, 0);                      // ts
	p = xkcp_encode32u(p, 0);                      // sn
	p = xkcp_encode32u(p, kcp->rcv_nxt);           // una
	p = xkcp_encode32u(p, 0);                      // len
	ikcp_output(kcp, buf, (int)(p - buf));
}


static inline void 
xkcp_make_nonce(uint8_t *nonce, uint32_t conv, uint32_t seq, uint8_t dir) {
	memset(nonce, 0, crypto_aead_chacha20poly1305_ietf_NPUBBYTES);
	memcpy(nonce + 0, &conv, sizeof(conv));
	memcpy(nonce + 4, &seq, sizeof(seq));
	nonce[8] = dir;
}


void
xkcp_kx_keygen(xkcpcb *kcp) {
	crypto_kx_keypair(kcp->eph_pk, kcp->eph_sk);
}


int
ikcp_kx_server(xkcpcb *kcp, const uint8_t *client_pk, uint8_t *out_server_pk) {
	uint8_t sk[crypto_kx_SECRETKEYBYTES];
	if (crypto_kx_keypair(out_server_pk, sk) != 0) {
		return -1;
	}

	if (crypto_kx_server_session_keys(kcp->rx_key, kcp->tx_key, out_server_pk, sk, client_pk) != 0) {
		return -1;
	}
		
	kcp->snd_dir = 1;  // S2C
	kcp->rcv_dir = 0;  // C2S
	kcp->last_snd_seq = 0;
	kcp->last_rcv_seq = 0;
	kcp->has_key = 1;

	return 0;
}


int
ikcp_kx_client(xkcpcb *kcp, const uint8_t *server_pk) {
	if (crypto_kx_client_session_keys(kcp->rx_key, kcp->tx_key, kcp->eph_pk, kcp->eph_sk, server_pk) != 0)
		return -1;
	kcp->snd_dir = 0;  // C2S
	kcp->rcv_dir = 1;  // S2C
	kcp->last_snd_seq = 0;
	kcp->last_rcv_seq = 0;
	kcp->has_key = 1;
	return 0;
}


//---------------------------------------------------------------------
// create a new kcpcb
//---------------------------------------------------------------------
xkcpcb*
xkcp_create(uint32_t conv, void *user) {
	xkcpcb *kcp = (xkcpcb*)mi_malloc(sizeof(struct XKCPCB));
	if (kcp == NULL) {
		return NULL;
	}

	kcp->conv = conv;
	kcp->user = user;
	kcp->snd_una = 0;
	kcp->snd_nxt = 0;
	kcp->rcv_nxt = 0;
	kcp->ts_recent = 0;
	kcp->ts_lastack = 0;
	kcp->ts_probe = 0;
	kcp->probe_wait = 0;
	kcp->snd_wnd = XKCP_WND_SND;
	kcp->rcv_wnd = XKCP_WND_RCV;
	kcp->rmt_wnd = XKCP_WND_RCV;
	kcp->cwnd = 0;
	kcp->incr = 0;
	kcp->probe = 0;
	kcp->mtu = XKCP_MTU_DEF;
	kcp->mss = kcp->mtu - XKCP_OVERHEAD;
	kcp->stream = 0;

	kcp->buffer = (uint8_t*)mi_malloc((kcp->mtu + XKCP_OVERHEAD) * 3);
	if (kcp->buffer == NULL) {
		mi_free(kcp);
		return NULL;
	}

	xqueue_init(&kcp->snd_queue);
	xqueue_init(&kcp->rcv_queue);
	xqueue_init(&kcp->snd_buf);
	xqueue_init(&kcp->rcv_buf);
	kcp->nrcv_buf = 0;
	kcp->nsnd_buf = 0;
	kcp->nrcv_que = 0;
	kcp->nsnd_que = 0;
	kcp->state = 0;
	kcp->acklist = NULL;
	kcp->ackblock = 0;
	kcp->ackcount = 0;
	kcp->ackedlen = 0;
	kcp->rx_srtt = 0;
	kcp->rx_rttval = 0;
	kcp->rx_rto = XKCP_RTO_DEF;
	kcp->rx_minrto = XKCP_RTO_MIN;
	kcp->current = 0;
	kcp->interval = XKCP_INTERVAL;
	kcp->ts_flush = XKCP_INTERVAL;
	kcp->nodelay = 0;
	kcp->updated = 0;
	kcp->logmask = 0;
	kcp->ssthresh = XKCP_THRESH_INIT;
	kcp->fastresend = 0;
	kcp->fastlimit = XKCP_FASTACK_LIMIT;
	kcp->nocwnd = 0;
	kcp->xmit = 0;
	kcp->dead_link = XKCP_DEADLINK;
	kcp->output = NULL;
	kcp->ccops = NULL;
	kcp->congest = NULL;
	kcp->writelog = NULL;
	kcp->on_regist = NULL;
	kcp->on_regist_rsp = NULL;
	kcp->on_rst = NULL;
	kcp->on_kic = NULL;
	kcp->has_regist = 0;
	kcp->regist_rsp_len = 0;
	kcp->last_snd_ms = 0;
	kcp->last_rcv_ms = 0;
	kcp->ping_interval = 0;
	kcp->dead_timeout = 0;
	kcp->dead = 0;
	kcp->on_timeout = NULL;
	kcp->last_snd_seq = 0;
	kcp->last_rcv_seq = 0;
	kcp->snd_dir = 0;
	kcp->rcv_dir = 0;
	kcp->has_key = 0;

	return kcp;
}


//---------------------------------------------------------------------
// release a kcpcb
//---------------------------------------------------------------------
void
xkcp_release(xkcpcb *kcp) {
	XKCPSEG *seg;
	assert(kcp);
	if (kcp) {
		if (kcp->ccops && kcp->ccops->release) {
			kcp->ccops->release(kcp);
		}

		while (!xqueue_is_empty(&kcp->snd_buf)) {
			seg = xqueue_entry(kcp->snd_buf.next, XKCPSEG, node);
			xqueue_del(&seg->node);
			xkcp_segment_delete(kcp, seg);
		}

		while (!xqueue_is_empty(&kcp->rcv_buf)) {
			seg = xqueue_entry(kcp->rcv_buf.next, XKCPSEG, node);
			xqueue_del(&seg->node);
			xkcp_segment_delete(kcp, seg);
		}

		while (!xqueue_is_empty(&kcp->snd_queue)) {
			seg = xqueue_entry(kcp->snd_queue.next, XKCPSEG, node);
			xqueue_del(&seg->node);
			xkcp_segment_delete(kcp, seg);
		}

		while (!xqueue_is_empty(&kcp->rcv_queue)) {
			seg = xqueue_entry(kcp->rcv_queue.next, XKCPSEG, node);
			xqueue_del(&seg->node);
			xkcp_segment_delete(kcp, seg);
		}

		if (kcp->buffer) {
			mi_free(kcp->buffer);
		}

		if (kcp->acklist) {
			mi_free(kcp->acklist);
		}

		kcp->nrcv_buf = 0;
		kcp->nsnd_buf = 0;
		kcp->nrcv_que = 0;
		kcp->nsnd_que = 0;
		kcp->ackcount = 0;
		kcp->buffer = NULL;
		kcp->acklist = NULL;
		mi_free(kcp);
	}
}


//---------------------------------------------------------------------
// [XKCP] reset transport state (keep conv & config) —— 用于 REGIST/重连; 复位即作废密钥
//---------------------------------------------------------------------
void
xkcp_reset(xkcpcb *kcp) {
	XKCPSEG *seg;
	assert(kcp);
	if (kcp == NULL) {
		return;
	}

	while (!xqueue_is_empty(&kcp->snd_buf)) {
		seg = xqueue_entry(kcp->snd_buf.next, XKCPSEG, node);
		xqueue_del(&seg->node);
		xkcp_segment_delete(kcp, seg);
	}

	while (!xqueue_is_empty(&kcp->rcv_buf)) {
		seg = xqueue_entry(kcp->rcv_buf.next, XKCPSEG, node);
		xqueue_del(&seg->node);
		xkcp_segment_delete(kcp, seg);
	}

	while (!xqueue_is_empty(&kcp->snd_queue)) {
		seg = xqueue_entry(kcp->snd_queue.next, XKCPSEG, node);
		xqueue_del(&seg->node);
		xkcp_segment_delete(kcp, seg);
	}

	while (!xqueue_is_empty(&kcp->rcv_queue)) {
		seg = xqueue_entry(kcp->rcv_queue.next, XKCPSEG, node);
		xqueue_del(&seg->node);
		xkcp_segment_delete(kcp, seg);
	}

	if (kcp->acklist) {
		mi_free(kcp->acklist);
	}

	kcp->acklist        = NULL;
	kcp->ackblock       = 0;
	kcp->ackcount       = 0;
	kcp->ackedlen       = 0;
	kcp->snd_una        = 0;
	kcp->snd_nxt        = 0;
	kcp->rcv_nxt        = 0;
	kcp->nrcv_buf       = 0;
	kcp->nsnd_buf       = 0;
	kcp->nrcv_que       = 0;
	kcp->nsnd_que       = 0;
	kcp->state          = 0;
	kcp->ts_recent      = 0;
	kcp->ts_lastack     = 0;
	kcp->ts_probe       = 0;
	kcp->probe_wait     = 0;
	kcp->probe          = 0;
	kcp->rmt_wnd        = XKCP_WND_RCV;
	kcp->cwnd           = 0;
	kcp->incr           = 0;
	kcp->ssthresh       = XKCP_THRESH_INIT;
	kcp->rx_srtt        = 0;
	kcp->rx_rttval      = 0;
	kcp->rx_rto         = XKCP_RTO_DEF;
	kcp->xmit           = 0;
	kcp->has_regist     = 0;
	kcp->regist_rsp_len = 0;
	kcp->last_snd_ms    = kcp->current;
	kcp->last_rcv_ms    = kcp->current;
	kcp->dead           = 0;
	kcp->has_key        = 0;
	kcp->last_snd_seq   = 0;
	kcp->last_rcv_seq   = 0;
}


int
ikcp_recv(xkcpcb *kcp, uint8_t *buffer, int len) {
	struct XQUEUEHEAD *p;
	int recover = 0;
	uint8_t *const savedbuf = buffer;
	assert(kcp);

	if (xqueue_is_empty(&kcp->rcv_queue)) {
		return -1;          // 队列空, 没有数据
	}

	int peeksize = xkcp_peeksize(kcp);

	if (peeksize < 0)  {
		return -2;          // 消息还没收全
	}

	if (peeksize > len) {
		return -3;          // buffer 太小
	}

	if (kcp->nrcv_que >= kcp->rcv_wnd) {
		// 拥塞控制触发流量控制
		recover = 1;
	}

	// 组包
	XKCPSEG *seg;
	for (len = 0, p = kcp->rcv_queue.next; p != &kcp->rcv_queue; ) {
		int fragment;
		seg = xqueue_entry(p, XKCPSEG, node);
		p = p->next;

		if (buffer) {
			memcpy(buffer, seg->data, seg->len);
			buffer += seg->len;
		}

		len += seg->len;
		fragment = seg->frg;

		if (xkcp_canlog(kcp, XKCP_LOG_RECV)) {
			xkcp_log(kcp, XKCP_LOG_RECV, "recv sn=%lu", (unsigned long)seg->sn);
		}

		xqueue_del(&seg->node);
		xkcp_segment_delete(kcp, seg);
		kcp->nrcv_que--;

		if (fragment == 0) {
			break;
		}
	}

	assert(len == peeksize);

	// 移除已到达的数据
	while (!xqueue_is_empty(&kcp->rcv_buf)) {
		seg = xqueue_entry(kcp->rcv_buf.next, XKCPSEG, node);
		if (seg->sn == kcp->rcv_nxt && kcp->nrcv_que < kcp->rcv_wnd) {
			xqueue_del(&seg->node);
			kcp->nrcv_buf--;
			xqueue_add_tail(&seg->node, &kcp->rcv_queue);
			kcp->nrcv_que++;
			kcp->rcv_nxt++;
		} else {
			break;
		}
	}

	// 进行快速恢复
	if (kcp->nrcv_que < kcp->rcv_wnd && recover) {
		// 告知对端当前接收窗口
		kcp->probe |= XKCP_ASK_TELL;
	}

	// AEAD 解密: 整条消息在用户 buffer 原地解密; 认证失败 = 篡改/错乱 → 返回 -4
	if (kcp->has_key && savedbuf != NULL && len >= (int)crypto_aead_chacha20poly1305_ietf_ABYTES) {
		uint8_t nonce[crypto_aead_chacha20poly1305_ietf_NPUBBYTES];
		int plen = len - (int)crypto_aead_chacha20poly1305_ietf_ABYTES;
		xkcp_make_nonce(nonce, kcp->conv, ++kcp->last_rcv_seq, kcp->rcv_dir);
		if (crypto_aead_chacha20poly1305_ietf_decrypt_detached(
				(uint8_t*)savedbuf, NULL,
				(const uint8_t*)savedbuf, (uint64_t)plen,
				(const uint8_t*)(savedbuf + plen), NULL, 0, nonce, kcp->rx_key) != 0) {
			return -4;
		}
		len = plen;
	}

	return len;
}


int
xkcp_peeksize(const xkcpcb *kcp) {
	struct XQUEUEHEAD *p;
	XKCPSEG *seg;
	int length = 0;

	assert(kcp);

	if (xqueue_is_empty(&kcp->rcv_queue)) {
		return -1;
	}

	seg = xqueue_entry(kcp->rcv_queue.next, XKCPSEG, node);
	if (seg->frg == 0) {
		return seg->len;
	}

	if (kcp->nrcv_que < seg->frg + 1) {
		return -1;
	}

	for (p = kcp->rcv_queue.next; p != &kcp->rcv_queue; p = p->next) {
		seg = xqueue_entry(p, XKCPSEG, node);
		length += seg->len;
		if (seg->frg == 0) {
			break;
		}
	}

	return length;
}


static int
ikcp_send_raw(xkcpcb *kcp, const uint8_t *buffer, int len) {
	XKCPSEG *seg;
	int count, i;
	int sent = 0;

	assert(kcp->mss > 0);
	if (len < 0) {
		return -1;
	}

	if (kcp->stream != 0) {
		// 流模式
		if (!xqueue_is_empty(&kcp->snd_queue)) {
			XKCPSEG *old = xqueue_entry(kcp->snd_queue.prev, XKCPSEG, node);
			if (old->len < kcp->mss) {
				int capacity = kcp->mss - old->len;
				int extend = (len < capacity)? len : capacity;
				seg = xkcp_segment_new(kcp, old->len + extend);
				assert(seg);
				if (seg == NULL) {
					return -2;
				}

				xqueue_add_tail(&seg->node, &kcp->snd_queue);
				memcpy(seg->data, old->data, old->len);
				if (buffer) {
					memcpy(seg->data + old->len, buffer, extend);
					buffer += extend;
				}

				seg->len = old->len + extend;
				seg->frg = 0;
				len -= extend;
				xqueue_del_init(&old->node);
				xkcp_segment_delete(kcp, old);
				sent = extend;
			}
		}

		if (len <= 0) {
			return sent;
		}
	}

	if (len <= (int)kcp->mss) {
		count = 1;
	}
	else {
		count = (len + kcp->mss - 1) / kcp->mss;
	}

	if (count >= XKCP_WND_RCV) {
		if (kcp->stream != 0 && sent > 0)  {
			return sent;
		}
		return -2;
	}

	if (count == 0) {
		count = 1;
	}

	// fragment
	for (i = 0; i < count; i++) {
		int size = len > (int)kcp->mss ? (int)kcp->mss : len;
		seg = xkcp_segment_new(kcp, size);
		assert(seg);
		if (seg == NULL) {
			return -2;
		}

		if (buffer && len > 0) {
			memcpy(seg->data, buffer, size);
		}

		seg->len = size;
		seg->frg = (kcp->stream == 0)? (count - i - 1) : 0;
		xqueue_init(&seg->node);
		xqueue_add_tail(&seg->node, &kcp->snd_queue);
		kcp->nsnd_que++;

		if (buffer) {
			buffer += size;
		}

		len -= size;
		sent += size;
	}

	return sent;
}


int 
xkcp_send(xkcpcb *kcp, const uint8_t *buffer, int len) {
	if (kcp->has_key && buffer && len > 0) {
		int clen = len + (int)crypto_aead_chacha20poly1305_ietf_ABYTES;
		uint8_t nonce[crypto_aead_chacha20poly1305_ietf_NPUBBYTES];
		uint8_t *ct = (uint8_t*)mi_malloc(clen);

		if (ct == NULL) {
			return -1;
		}
	
		xkcp_make_nonce(nonce, kcp->conv, ++kcp->last_snd_seq, kcp->snd_dir);
		if (crypto_aead_chacha20poly1305_ietf_encrypt_detached((uint8_t*)ct, (uint8_t*)(ct + len), NULL, (const uint8_t*)buffer, (uint64_t)len, NULL, 0, NULL, nonce, kcp->tx_key) < 0) {
			mi_free(ct);
			return -1;
		}

		int ret = ikcp_send_raw(kcp, ct, clen);
		mi_free(ct);
		return ret;
	}

	return ikcp_send_raw(kcp, buffer, len);
}


//---------------------------------------------------------------------
// parse ack
//---------------------------------------------------------------------
// 用一个 RTT 样本更新平滑 RTT(rx_srtt)与重传超时(rx_rto)
static void 
xkcp_update_ack(xkcpcb *kcp, int32_t rtt) {
	int32_t rto = 0;
	if (kcp->rx_srtt == 0) {
		kcp->rx_srtt = rtt;
		kcp->rx_rttval = rtt / 2;
	} else {
		long delta = rtt - kcp->rx_srtt;
		if (delta < 0) delta = -delta;
		kcp->rx_rttval = (3 * kcp->rx_rttval + delta) / 4;
		kcp->rx_srtt = (7 * kcp->rx_srtt + rtt) / 8;
		if (kcp->rx_srtt < 1) kcp->rx_srtt = 1;
	}
	rto = kcp->rx_srtt + _xmax_(kcp->interval, 4 * kcp->rx_rttval);
	kcp->rx_rto = _xbound_(kcp->rx_minrto, rto, XKCP_RTO_MAX);
	if (kcp->ccops && kcp->ccops->on_rtt) {
		kcp->ccops->on_rtt(kcp, rtt);
	}
}

// 把 snd_una 推进到 snd_buf 首段的 sn(无未确认段时 = snd_nxt)
static void
xkcp_shrink_buf(xkcpcb *kcp) {
	struct XQUEUEHEAD *p = kcp->snd_buf.next;
	if (p != &kcp->snd_buf) {
		XKCPSEG *seg = xqueue_entry(p, XKCPSEG, node);
		kcp->snd_una = seg->sn;
	}	else {
		kcp->snd_una = kcp->snd_nxt;
	}
}

// 收到某个 sn 的 ACK: 从 snd_buf 删掉该段(已确认)
static void
xkcp_parse_ack(xkcpcb *kcp, uint32_t sn) {
	struct XQUEUEHEAD *p, *next;
	int32_t pkt_rtt;

	if (_xtimediff(sn, kcp->snd_una) < 0 || _xtimediff(sn, kcp->snd_nxt) >= 0)
		return;

	for (p = kcp->snd_buf.next; p != &kcp->snd_buf; p = next) {
		XKCPSEG *seg = xqueue_entry(p, XKCPSEG, node);
		next = p->next;
		if (sn == seg->sn) {
			kcp->ackedlen += seg->len;
			if (kcp->ccops && kcp->ccops->on_pkt_acked) {
				pkt_rtt = -1;
				if (_xtimediff(kcp->current, seg->ts) >= 0) {
					pkt_rtt = _xtimediff(kcp->current, seg->ts);
				}
				kcp->ccops->on_pkt_acked(kcp, seg->sn, seg->ts,
						seg->len, pkt_rtt, seg->xmit);
			}
			xqueue_del(p);
			xkcp_segment_delete(kcp, seg);
			kcp->nsnd_buf--;
			break;
		}
		if (_xtimediff(sn, seg->sn) < 0) {
			break;
		}
	}
}

// 按对端 una 批量确认: 删掉 snd_buf 里所有 sn < una 的段(una 之前对端都收到了)
static void
xkcp_parse_una(xkcpcb *kcp, uint32_t una) {
	struct XQUEUEHEAD *p, *next;
	for (p = kcp->snd_buf.next; p != &kcp->snd_buf; p = next) {
		XKCPSEG *seg = xqueue_entry(p, XKCPSEG, node);
		next = p->next;
		if (_xtimediff(una, seg->sn) > 0) {
			kcp->ackedlen += seg->len;
			if (kcp->ccops && kcp->ccops->on_pkt_acked) {
				kcp->ccops->on_pkt_acked(kcp, seg->sn, seg->ts,
						seg->len, -1, seg->xmit);
			}
			xqueue_del(p);
			xkcp_segment_delete(kcp, seg);
			kcp->nsnd_buf--;
		}	else {
			break;
		}
	}
}

// 给 sn 之前仍未确认的段累计"被跨越"计数(fastack); 达 fastresend 阈值会触发快速重传
static void xkcp_parse_fastack(xkcpcb *kcp, uint32_t sn, uint32_t ts)
{
	struct XQUEUEHEAD *p, *next;

	if (_xtimediff(sn, kcp->snd_una) < 0 || _xtimediff(sn, kcp->snd_nxt) >= 0)
		return;

	for (p = kcp->snd_buf.next; p != &kcp->snd_buf; p = next) {
		XKCPSEG *seg = xqueue_entry(p, XKCPSEG, node);
		next = p->next;
		if (_xtimediff(sn, seg->sn) < 0) {
			break;
		}
		else if (sn != seg->sn) {
		#ifndef IKCP_FASTACK_CONSERVE
			seg->fastack++;
		#else
			if (_xtimediff(ts, seg->ts) >= 0) {
				seg->fastack++;
			}
		#endif
		}
	}
}


//---------------------------------------------------------------------
// ack append
//---------------------------------------------------------------------
// 记录一个待回的 ACK(sn,ts) 进 acklist, 等 flush 时统一发出
static void
xkcp_ack_push(xkcpcb *kcp, uint32_t sn, uint32_t ts) {
	uint32_t newsize = kcp->ackcount + 1;
	uint32_t *ptr;

	if (newsize > kcp->ackblock) {
		uint32_t *acklist;
		uint32_t newblock;

		for (newblock = 8; newblock < newsize; newblock <<= 1);
		acklist = (uint32_t*)mi_malloc(newblock * sizeof(uint32_t) * 2);

		if (acklist == NULL) {
			assert(acklist != NULL);
			abort();
		}

		if (kcp->acklist != NULL) {
			uint32_t x;
			for (x = 0; x < kcp->ackcount; x++) {
				acklist[x * 2 + 0] = kcp->acklist[x * 2 + 0];
				acklist[x * 2 + 1] = kcp->acklist[x * 2 + 1];
			}
			mi_free(kcp->acklist);
		}

		kcp->acklist = acklist;
		kcp->ackblock = newblock;
	}

	ptr = &kcp->acklist[kcp->ackcount * 2];
	ptr[0] = sn;
	ptr[1] = ts;
	kcp->ackcount++;
}

// 取出 acklist 里第 p 个待回 ACK 的 sn/ts
static void
xkcp_ack_get(const xkcpcb *kcp, int p, uint32_t *sn, uint32_t *ts) {
	if (sn) sn[0] = kcp->acklist[p * 2 + 0];
	if (ts) ts[0] = kcp->acklist[p * 2 + 1];
}


//---------------------------------------------------------------------
// parse data
//---------------------------------------------------------------------
// 收到一个数据段: 按 sn 去重+排序插入 rcv_buf, 把从 rcv_nxt 起连续的段搬进 rcv_queue
void
xkcp_parse_data(xkcpcb *kcp, XKCPSEG *newseg) {
	struct XQUEUEHEAD *p, *prev;
	uint32_t sn = newseg->sn;
	int repeat = 0;
	
	if (_xtimediff(sn, kcp->rcv_nxt + kcp->rcv_wnd) >= 0 ||
		_xtimediff(sn, kcp->rcv_nxt) < 0) {
		xkcp_segment_delete(kcp, newseg);
		return;
	}

	for (p = kcp->rcv_buf.prev; p != &kcp->rcv_buf; p = prev) {
		XKCPSEG *seg = xqueue_entry(p, XKCPSEG, node);
		prev = p->prev;
		if (seg->sn == sn) {
			repeat = 1;
			break;
		}
		if (_xtimediff(sn, seg->sn) > 0) {
			break;
		}
	}

	if (repeat == 0) {
		xqueue_init(&newseg->node);
		xqueue_add(&newseg->node, p);
		kcp->nrcv_buf++;
	} else {
		xkcp_segment_delete(kcp, newseg);
	}

#if 0
	ikcp_qprint("rcvbuf", &kcp->rcv_buf);
	printf("rcv_nxt=%lu\n", kcp->rcv_nxt);
#endif

	// move available data from rcv_buf -> rcv_queue
	while (! xqueue_is_empty(&kcp->rcv_buf)) {
		XKCPSEG *seg = xqueue_entry(kcp->rcv_buf.next, XKCPSEG, node);
		if (seg->sn == kcp->rcv_nxt && kcp->nrcv_que < kcp->rcv_wnd) {
			xqueue_del(&seg->node);
			kcp->nrcv_buf--;
			xqueue_add_tail(&seg->node, &kcp->rcv_queue);
			kcp->nrcv_que++;
			kcp->rcv_nxt++;
		}	else {
			break;
		}
	}

#if 0
	ikcp_qprint("queue", &kcp->rcv_queue);
	printf("rcv_nxt=%lu\n", kcp->rcv_nxt);
#endif

#if 1
//	printf("snd(buf=%d, queue=%d)\n", kcp->nsnd_buf, kcp->nsnd_que);
//	printf("rcv(buf=%d, queue=%d)\n", kcp->nrcv_buf, kcp->nrcv_que);
#endif
}


// [XKCP] 数据 cmd(PUSH/ACK/WASK/WINS)的公共窗口处理: 更新对端窗口 + 按 una 确认 + 收缩 snd_buf
static inline void
xkcp_input_window(xkcpcb *kcp, uint16_t wnd, uint32_t una) {
	kcp->rmt_wnd = wnd;
	xkcp_parse_una(kcp, una);
	xkcp_shrink_buf(kcp);
}


//---------------------------------------------------------------------
// [XKCP] 控制 cmd 的处理(ikcp_input 的 switch 分发到这里)。data/len = 该控制段 payload。
//---------------------------------------------------------------------

// 服务端收 REGIST_REQ: 传输层去重 —— 同一握手(同 id)的重发直接重发缓存 RSP, 不上抛;
// 新握手才上抛 on_regist 回调, 把回调产出的 RSP 缓存并发出。
static void
xkcp_on_reg_req(xkcpcb *kcp, const uint8_t *data, int len) {
	// 同一握手的重发: 直接重发缓存的 RSP
	if (kcp->has_regist && len >= XKCP_REGIST_ID_LEN &&
		memcmp(data, kcp->regist_id, XKCP_REGIST_ID_LEN) == 0) {
		if (kcp->regist_rsp_len > 0 && kcp->output) {
			kcp->output(kcp->regist_rsp, kcp->regist_rsp_len, kcp);
		}
		return;
	}

	// 新握手: 回调产出 RSP, 缓存 id + RSP 并发出
	if (kcp->on_regist) {
		uint8_t out_data[XKCP_REGIST_RSP_MAX];
		int out_len = 0;
		kcp->on_regist(data, len, kcp, out_data, &out_len);
		if (out_len <= 0) {
			return;
		}
		if (out_len > XKCP_REGIST_RSP_MAX) {
			out_len = XKCP_REGIST_RSP_MAX;
		}
		if (len >= XKCP_REGIST_ID_LEN) {
			memcpy(kcp->regist_id, data, XKCP_REGIST_ID_LEN);
			kcp->has_regist = 1;
		}
		memcpy(kcp->regist_rsp, out_data, out_len);
		kcp->regist_rsp_len = out_len;
		if (kcp->output) {
			kcp->output(out_data, out_len, kcp);
		}
	}
}

// 客户端收 REGIST_RSP: 上抛回调(取服务端公钥、算密钥、置 authed)
static void
xkcp_on_reg_rsp(xkcpcb *kcp, const uint8_t *data, int len) {
	if (kcp->on_regist_rsp) {
		kcp->on_regist_rsp(data, len, kcp);
	}
}

// 客户端收 RST(顶号): 上抛回调
static void
xkcp_on_rst(xkcpcb *kcp, const uint8_t *data, int len) {
	if (kcp->on_rst) {
		kcp->on_rst(data, len, kcp);
	}
}

// 客户端收 KIC(被踢): 上抛回调
static void
xkcp_on_kic(xkcpcb *kcp, const uint8_t *data, int len) {
	if (kcp->on_kic) {
		kcp->on_kic(data, len, kcp);
	}
}

// 收 PING: 回 PONG
static void
xkcp_on_ping(xkcpcb *kcp) {
	xkcp_output_ctrl(kcp, XKCP_CMD_PONG);
}

// 收 PONG: 不处理(last_rcv_ms 已在 ikcp_input 入口刷新)
static void
xkcp_on_pong(xkcpcb *kcp) {
	(void)kcp;
}

// 收 WASK(窗口探测): 标记下次 flush 回 WINS 告知本端窗口
static void
xkcp_on_wask(xkcpcb *kcp) {
	kcp->probe |= XKCP_ASK_TELL;
	if (xkcp_canlog(kcp, XKCP_LOG_IN_PROBE)) {
		xkcp_log(kcp, XKCP_LOG_IN_PROBE, "input probe");
	}
}

// 收 WINS(对端窗口通告): 窗口已在 ikcp_input_window 里更新, 这里仅 log
static void
xkcp_on_wins(xkcpcb *kcp, uint16_t wnd) {
	if (xkcp_canlog(kcp, XKCP_LOG_IN_WINS)) {
		xkcp_log(kcp, XKCP_LOG_IN_WINS, "input wins: %lu", (unsigned long)wnd);
	}
}


int
xkcp_input(xkcpcb *kcp, const uint8_t *data, int size) {
	uint32_t prev_una = kcp->snd_una;
	uint32_t prev_nsnd_buf = kcp->nsnd_buf;
	uint32_t acked_segs, prior_in_flight;
	uint32_t maxack = 0, latest_ts = 0;
	int flag = 0;

	kcp->ackedlen = 0;

	if (xkcp_canlog(kcp, XKCP_LOG_INPUT)) {
		xkcp_log(kcp, XKCP_LOG_INPUT, "[RI] %d bytes", size);
	}

	if (data == NULL || size < (int)XKCP_OVERHEAD) {
		return -1;
	}

	kcp->last_rcv_ms = kcp->current;

	while (1) {
		uint32_t ts, sn, len, una, conv;
		uint16_t wnd;
		uint8_t cmd, frg;
		XKCPSEG *seg;

		if (size < (int)XKCP_OVERHEAD) {
			break;
		}

		data = xkcp_decode32u(data, &conv);
		if (conv != kcp->conv) {
			return -1;
		}

		data = xkcp_decode8u(data, &cmd);
		data = xkcp_decode8u(data, &frg);
		data = xkcp_decode16u(data, &wnd);
		data = xkcp_decode32u(data, &ts);
		data = xkcp_decode32u(data, &sn);
		data = xkcp_decode32u(data, &una);
		data = xkcp_decode32u(data, &len);

		size -= XKCP_OVERHEAD;

		if ((long)size < (long)len || (int)len < 0) {
			return -2;
		}

		switch (cmd) {
		case XKCP_CMD_REGIST_REQ:
			xkcp_on_reg_req(kcp, data, (int)len);
			break;

		case XKCP_CMD_REGIST_RSP:
			xkcp_on_reg_rsp(kcp, data, (int)len);
			break;

		case XKCP_CMD_RST:
			xkcp_on_rst(kcp, data, (int)len);
			break;

		case XKCP_CMD_KIC:
			xkcp_on_kic(kcp, data, (int)len);
			break;

		case XKCP_CMD_PING:
			xkcp_on_ping(kcp);
			break;

		case XKCP_CMD_PONG:
			xkcp_on_pong(kcp);
			break;

		case XKCP_CMD_WASK:
			xkcp_input_window(kcp, wnd, una);
			xkcp_on_wask(kcp);
			break;

		case XKCP_CMD_WINS:
			xkcp_input_window(kcp, wnd, una);
			xkcp_on_wins(kcp, wnd);
			break;

		case XKCP_CMD_ACK:
			xkcp_input_window(kcp, wnd, una);
			if (_xtimediff(kcp->current, ts) >= 0) {
				xkcp_update_ack(kcp, _xtimediff(kcp->current, ts));
			}
			xkcp_parse_ack(kcp, sn);
			xkcp_shrink_buf(kcp);
			if (flag == 0) {
				flag = 1;
				maxack = sn;
				latest_ts = ts;
			}	else {
				if (_xtimediff(sn, maxack) > 0) {
				#ifndef IKCP_FASTACK_CONSERVE
					maxack = sn;
					latest_ts = ts;
				#else
					if (_xtimediff(ts, latest_ts) > 0) {
						maxack = sn;
						latest_ts = ts;
					}
				#endif
				}
			}
			if (xkcp_canlog(kcp, XKCP_LOG_IN_ACK)) {
				xkcp_log(kcp, XKCP_LOG_IN_ACK, 
					"input ack: sn=%lu rtt=%ld rto=%ld", (unsigned long)sn, 
					(long)_xtimediff(kcp->current, ts),
					(long)kcp->rx_rto);
			}
			break;

		case XKCP_CMD_PUSH:
			xkcp_input_window(kcp, wnd, una);
			if (xkcp_canlog(kcp, XKCP_LOG_IN_DATA)) {
				xkcp_log(kcp, XKCP_LOG_IN_DATA, 
					"input psh: sn=%lu ts=%lu", (unsigned long)sn, (unsigned long)ts);
			}
			if (_xtimediff(sn, kcp->rcv_nxt + kcp->rcv_wnd) < 0) {
				xkcp_ack_push(kcp, sn, ts);
				if (_xtimediff(sn, kcp->rcv_nxt) >= 0) {
					seg = xkcp_segment_new(kcp, len);
					seg->conv = conv;
					seg->cmd = cmd;
					seg->frg = frg;
					seg->wnd = wnd;
					seg->ts = ts;
					seg->sn = sn;
					seg->una = una;
					seg->len = len;

					if (len > 0) {
						memcpy(seg->data, data, len);
					}

					xkcp_parse_data(kcp, seg);
				}
			}
			break;

		default:
			return -3;
		}

		data += len;
		size -= len;
	}

	if (flag != 0) {
		xkcp_parse_fastack(kcp, maxack, latest_ts);
	}

	if (_xtimediff(kcp->snd_una, prev_una) > 0) {
		acked_segs = kcp->snd_una - prev_una;
		prior_in_flight = prev_nsnd_buf;
		if (kcp->ccops && kcp->ccops->on_ack) {
			kcp->ccops->on_ack(kcp, acked_segs, kcp->ackedlen, prior_in_flight);
		}
		else {
			if (kcp->cwnd < kcp->rmt_wnd) {
				uint32_t mss = kcp->mss;
				if (kcp->cwnd < kcp->ssthresh) {
					kcp->cwnd++;
					kcp->incr += mss;
				}	else {
					if (kcp->incr < mss) {
						kcp->incr = mss;
					}
					kcp->incr += (mss * mss) / kcp->incr + (mss / 16);
					if ((kcp->cwnd + 1) * mss <= kcp->incr) {
					#if 1
						kcp->cwnd = (kcp->incr + mss - 1) / ((mss > 0)? mss : 1);
					#else
						kcp->cwnd++;
					#endif
					}
				}
				if (kcp->cwnd > kcp->rmt_wnd) {
					kcp->cwnd = kcp->rmt_wnd;
					kcp->incr = kcp->rmt_wnd * mss;
				}
			}
		}
	}

	return 0;
}


//---------------------------------------------------------------------
// ikcp_encode_seg
//---------------------------------------------------------------------
static uint8_t*
xkcp_encode_seg(uint8_t *ptr, const XKCPSEG *seg) {
	ptr = xkcp_encode32u(ptr, seg->conv);
	ptr = xkcp_encode8u(ptr, (uint8_t)seg->cmd);
	ptr = xkcp_encode8u(ptr, (uint8_t)seg->frg);
	ptr = xkcp_encode16u(ptr, (uint16_t)seg->wnd);
	ptr = xkcp_encode32u(ptr, seg->ts);
	ptr = xkcp_encode32u(ptr, seg->sn);
	ptr = xkcp_encode32u(ptr, seg->una);
	ptr = xkcp_encode32u(ptr, seg->len);
	return ptr;
}

static int 
xkcp_wnd_unused(const xkcpcb *kcp) {
	if (kcp->nrcv_que < kcp->rcv_wnd) {
		return kcp->rcv_wnd - kcp->nrcv_que;
	}
	return 0;
}


//---------------------------------------------------------------------
// ikcp_flush
//---------------------------------------------------------------------
// KCP 核心: 把待回 ACK、窗口探测、snd_queue/snd_buf 里的数据段组包发出, 并处理重传与拥塞
void
xkcp_flush(xkcpcb *kcp) {
	uint32_t current = kcp->current;
	uint8_t *buffer = kcp->buffer;
	uint8_t *ptr = buffer;
	int count, size, i;
	uint32_t resent, cwnd;
	uint32_t rtomin;
	uint32_t prior_cwnd;
	uint32_t eff_cwnd, cur_inflight;
	int32_t pacing_budget = -1;
	struct XQUEUEHEAD *p;
	int change = 0;
	int lost = 0;
	XKCPSEG seg;

	// 'ikcp_update' hasn't been called yet. 
	if (kcp->updated == 0) {
		return;
	}

	if (kcp->ccops && kcp->ccops->on_tick) {
		kcp->ccops->on_tick(kcp);
	}

	if (kcp->ccops && kcp->ccops->pacing_rate) {
		pacing_budget = (int32_t)kcp->ccops->pacing_rate(kcp);
	}

	prior_cwnd = kcp->cwnd;

	seg.conv = kcp->conv;
	seg.cmd = XKCP_CMD_ACK;
	seg.frg = 0;
	seg.wnd = xkcp_wnd_unused(kcp);
	seg.una = kcp->rcv_nxt;
	seg.len = 0;
	seg.sn = 0;
	seg.ts = 0;

	// flush acknowledges
	count = kcp->ackcount;
	for (i = 0; i < count; i++) {
		size = (int)(ptr - buffer);
		if (size + (int)XKCP_OVERHEAD > (int)kcp->mtu) {
			ikcp_output(kcp, buffer, size);
			ptr = buffer;
		}
		xkcp_ack_get(kcp, i, &seg.sn, &seg.ts);
		ptr = xkcp_encode_seg(ptr, &seg);
	}

	kcp->ackcount = 0;

	// probe window size (if remote window size equals zero)
	if (kcp->rmt_wnd == 0) {
		if (kcp->probe_wait == 0) {
			kcp->probe_wait = XKCP_PROBE_INIT;
			kcp->ts_probe = kcp->current + kcp->probe_wait;
		}	
		else {
			if (_xtimediff(kcp->current, kcp->ts_probe) >= 0) {
				if (kcp->probe_wait < XKCP_PROBE_INIT) 
					kcp->probe_wait = XKCP_PROBE_INIT;
				kcp->probe_wait += kcp->probe_wait / 2;
				if (kcp->probe_wait > XKCP_PROBE_LIMIT)
					kcp->probe_wait = XKCP_PROBE_LIMIT;
				kcp->ts_probe = kcp->current + kcp->probe_wait;
				kcp->probe |= XKCP_ASK_SEND;
			}
		}
	}	else {
		kcp->ts_probe = 0;
		kcp->probe_wait = 0;
	}

	// flush window probing commands
	if (kcp->probe & XKCP_ASK_SEND) {
		seg.cmd = XKCP_CMD_WASK;
		size = (int)(ptr - buffer);
		if (size + (int)XKCP_OVERHEAD > (int)kcp->mtu) {
			ikcp_output(kcp, buffer, size);
			ptr = buffer;
		}
		ptr = xkcp_encode_seg(ptr, &seg);
	}

	// flush window probing commands
	if (kcp->probe & XKCP_ASK_TELL) {
		seg.cmd = XKCP_CMD_WINS;
		size = (int)(ptr - buffer);
		if (size + (int)XKCP_OVERHEAD > (int)kcp->mtu) {
			ikcp_output(kcp, buffer, size);
			ptr = buffer;
		}
		ptr = xkcp_encode_seg(ptr, &seg);
	}

	kcp->probe = 0;

	// calculate window size
	cwnd = _xmin_(kcp->snd_wnd, kcp->rmt_wnd);
	if (kcp->ccops != NULL || kcp->nocwnd == 0) cwnd = _xmin_(kcp->cwnd, cwnd);

	// move data from snd_queue to snd_buf
	while (_xtimediff(kcp->snd_nxt, kcp->snd_una + cwnd) < 0) {
		XKCPSEG *newseg;
		if (xqueue_is_empty(&kcp->snd_queue)) {
			break;
		}

		newseg = xqueue_entry(kcp->snd_queue.next, XKCPSEG, node);

		xqueue_del(&newseg->node);
		xqueue_add_tail(&newseg->node, &kcp->snd_buf);
		kcp->nsnd_que--;
		kcp->nsnd_buf++;

		newseg->conv = kcp->conv;
		newseg->cmd = XKCP_CMD_PUSH;
		newseg->wnd = seg.wnd;
		newseg->ts = current;
		newseg->sn = kcp->snd_nxt++;
		newseg->una = kcp->rcv_nxt;
		newseg->resendts = current;
		newseg->rto = kcp->rx_rto;
		newseg->fastack = 0;
		newseg->xmit = 0;
	}

	// check on_app_limited
	if (kcp->ccops && kcp->ccops->on_app_limited) {
		if (xqueue_is_empty(&kcp->snd_queue)) {
			eff_cwnd = _xmin_(kcp->snd_wnd, kcp->rmt_wnd);
			eff_cwnd = _xmin_(kcp->cwnd, eff_cwnd);
			cur_inflight = kcp->nsnd_buf;
			if (cur_inflight < eff_cwnd) {
				kcp->ccops->on_app_limited(kcp, cur_inflight);
			}
		}
	}

	// calculate resent
	resent = (kcp->fastresend > 0) ? (uint32_t)kcp->fastresend : 0xffffffff;
	rtomin = (kcp->nodelay == 0) ? (kcp->rx_rto >> 3) : 0;

	// flush data segments
	for (p = kcp->snd_buf.next; p != &kcp->snd_buf; p = p->next) {
		XKCPSEG *segment = xqueue_entry(p, XKCPSEG, node);
		int needsend = 0;
		if (segment->xmit == 0) {
			needsend = 1;
			segment->xmit++;
			segment->rto = kcp->rx_rto;
			segment->resendts = current + segment->rto + rtomin;
		}
		else if (_xtimediff(current, segment->resendts) >= 0) {
			needsend = 1;
			segment->xmit++;
			kcp->xmit++;
			if (kcp->nodelay == 0) {
				segment->rto += _xmax_(segment->rto, (uint32_t)kcp->rx_rto);
			}	else {
				int32_t step = (kcp->nodelay < 2)? 
					((int32_t)(segment->rto)) : kcp->rx_rto;
				segment->rto += step / 2;
			}
			segment->resendts = current + segment->rto;
			lost = 1;
		}
		else if (segment->fastack >= resent) {
			if ((int)segment->xmit <= kcp->fastlimit || 
				kcp->fastlimit <= 0) {
				needsend = 1;
				segment->xmit++;
				segment->fastack = 0;
				segment->resendts = current + segment->rto;
				change++;
			}
		}

		if (needsend) {
			int need;
			segment->ts = current;
			segment->wnd = seg.wnd;
			segment->una = kcp->rcv_nxt;

			if (pacing_budget >= 0 && pacing_budget < (int32_t)segment->len) {
				break;
			}

			if (kcp->ccops && kcp->ccops->on_pkt_sent) {
				kcp->ccops->on_pkt_sent(kcp, segment->sn, current,
						segment->len, kcp->nsnd_buf, segment->xmit);
			}

			size = (int)(ptr - buffer);
			need = XKCP_OVERHEAD + segment->len;

			if (size + need > (int)kcp->mtu) {
				ikcp_output(kcp, buffer, size);
				ptr = buffer;
			}

			ptr = xkcp_encode_seg(ptr, segment);

			if (segment->len > 0) {
				memcpy(ptr, segment->data, segment->len);
				ptr += segment->len;
			}

			if (pacing_budget >= 0) {
				pacing_budget -= (int32_t)segment->len;
			}

			if (segment->xmit >= kcp->dead_link) {
				kcp->state = (uint32_t)-1;
			}
		}
	}

	// flash remaining segments
	size = (int)(ptr - buffer);
	if (size > 0) {
		ikcp_output(kcp, buffer, size);
	}

	// update ssthresh
	if (change) {
		if (kcp->ccops && kcp->ccops->on_fast_retransmit) {
			kcp->ccops->on_fast_retransmit(kcp, (uint32_t)change, 
					kcp->nsnd_buf, prior_cwnd);
		}
		else {
			uint32_t inflight = kcp->snd_nxt - kcp->snd_una;
			kcp->ssthresh = inflight / 2;
			if (kcp->ssthresh < XKCP_THRESH_MIN)
				kcp->ssthresh = XKCP_THRESH_MIN;
			kcp->cwnd = kcp->ssthresh + resent;
			kcp->incr = kcp->cwnd * kcp->mss;
		}
	}

	if (lost) {
		if (kcp->ccops && kcp->ccops->on_timeout) {
			kcp->ccops->on_timeout(kcp, prior_cwnd);
		}
		else {
			kcp->ssthresh = prior_cwnd / 2;
			if (kcp->ssthresh < XKCP_THRESH_MIN) {
				kcp->ssthresh = XKCP_THRESH_MIN;
			}

			kcp->cwnd = 1;
			kcp->incr = kcp->mss;
		}
	}

	if (kcp->cwnd < 1) {
		kcp->cwnd = 1;
		kcp->incr = kcp->mss;
	}
}


void
xkcp_update(xkcpcb *kcp, uint32_t current) {
	int32_t slap;

	kcp->current = current;

	if (kcp->updated == 0) {
		kcp->updated = 1;
		kcp->ts_flush = kcp->current;
		kcp->last_snd_ms = kcp->current;
		kcp->last_rcv_ms = kcp->current;
	}

	slap = _xtimediff(kcp->current, kcp->ts_flush);

	if (slap >= 10000 || slap < -10000) {
		kcp->ts_flush = kcp->current;
		slap = 0;
	}

	if (slap >= 0) {
		kcp->ts_flush += kcp->interval;
		if (_xtimediff(kcp->current, kcp->ts_flush) >= 0) {
			kcp->ts_flush = kcp->current + kcp->interval;
		}
		xkcp_flush(kcp);
	}

	// 保活/超时(用 current 这个 32bit 时钟; 阈值 0 = 关闭)
	if (kcp->dead_timeout > 0 && !kcp->dead &&
		_xtimediff(kcp->current, kcp->last_rcv_ms) > (int32_t)kcp->dead_timeout) {
		kcp->dead = 1;
		if (kcp->on_timeout) {
			kcp->on_timeout(kcp);
		}
	}
	else if (kcp->ping_interval > 0 && !kcp->dead &&
		_xtimediff(kcp->current, kcp->last_snd_ms) > (int32_t)kcp->ping_interval) {
		xkcp_output_ctrl(kcp, XKCP_CMD_PING);
	}
}


uint32_t
xkcp_check(const xkcpcb *kcp, uint32_t current) {
	uint32_t ts_flush = kcp->ts_flush;
	int32_t tm_flush = 0x7fffffff;
	int32_t tm_packet = 0x7fffffff;
	uint32_t minimal = 0;
	struct XQUEUEHEAD *p;

	if (kcp->updated == 0) {
		return current;
	}

	if (_xtimediff(current, ts_flush) >= 10000 ||
		_xtimediff(current, ts_flush) < -10000) {
		ts_flush = current;
	}

	if (_xtimediff(current, ts_flush) >= 0) {
		return current;
	}

	tm_flush = _xtimediff(ts_flush, current);

	for (p = kcp->snd_buf.next; p != &kcp->snd_buf; p = p->next) {
		const XKCPSEG *seg = xqueue_entry(p, const XKCPSEG, node);
		int32_t diff = _xtimediff(seg->resendts, current);
		if (diff <= 0) {
			return current;
		}
		if (diff < tm_packet) tm_packet = diff;
	}

	minimal = (uint32_t)(tm_packet < tm_flush ? tm_packet : tm_flush);
	if (minimal >= kcp->interval) minimal = kcp->interval;

	return current + minimal;
}


int
xkcp_setmtu(xkcpcb *kcp, int mtu) {
	uint8_t *buffer;
	if (mtu < 50 || mtu < (int)XKCP_OVERHEAD) {
		return -1;
	}

	buffer = (uint8_t*)mi_malloc((mtu + XKCP_OVERHEAD) * 3);
	if (buffer == NULL) {
		return -2;
	}

	kcp->mtu = mtu;
	kcp->mss = kcp->mtu - XKCP_OVERHEAD;
	mi_free(kcp->buffer);
	kcp->buffer = buffer;
	return 0;
}

int
ikcp_interval(xkcpcb *kcp, int interval) {
	if (interval > 5000) {
		interval = 5000;
	}
	else if (interval < 10) {
		interval = 10;
	}

	kcp->interval = interval;
	return 0;
}

int
xkcp_nodelay(xkcpcb *kcp, int nodelay, int interval, int resend, int nc) {
	if (nodelay >= 0) {
		kcp->nodelay = nodelay;
		if (nodelay) {
			kcp->rx_minrto = XKCP_RTO_NDL;	
		}	
		else {
			kcp->rx_minrto = XKCP_RTO_MIN;
		}
	}

	if (interval >= 0) {
		if (interval > 5000) interval = 5000;
		else if (interval < 10) interval = 10;
		kcp->interval = interval;
	}

	if (resend >= 0) {
		kcp->fastresend = resend;
	}

	if (nc >= 0) {
		kcp->nocwnd = nc;
	}

	return 0;
}


int
xkcp_wndsize(xkcpcb *kcp, int sndwnd, int rcvwnd) {
	if (kcp) {
		if (sndwnd > 0) {
			kcp->snd_wnd = sndwnd;
		}
		if (rcvwnd > 0) {   // must >= max fragment size
			kcp->rcv_wnd = _xmax_(rcvwnd, XKCP_WND_RCV);
		}
	}

	return 0;
}

int
xkcp_waitsnd(const xkcpcb *kcp) {
	return kcp->nsnd_buf + kcp->nsnd_que;
}


// read conv
uint32_t
xkcp_getconv(const void *ptr) {
	uint32_t conv;
	xkcp_decode32u((const uint8_t*)ptr, &conv);
	return conv;
}


//---------------------------------------------------------------------
// install congestion control
//---------------------------------------------------------------------
int
xkcp_setcc(xkcpcb *kcp, const struct XKCPOPS *ops) {
	assert(kcp);
	if (kcp->ccops && kcp->ccops->release) {
		kcp->ccops->release(kcp);
	}
	kcp->congest = NULL;
	kcp->ccops = ops;
	if (ops) {
		if (ops->init) {
			if (ops->init(kcp) < 0) {
				kcp->ccops = NULL;
				kcp->congest = NULL;
				if (kcp->cwnd < 1) kcp->cwnd = 1;
				kcp->incr = kcp->cwnd * kcp->mss;
				return -1;
			}
		}
	}
	else {
		if (kcp->cwnd < 1) kcp->cwnd = 1;
		kcp->incr = kcp->cwnd * kcp->mss;
		if (kcp->incr < kcp->mss) kcp->incr = kcp->mss;
	}
	return 0;
}