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
#include <time.h>


//=====================================================================
// KCP BASIC
//=====================================================================
#define XKCP_CMD_PUSH       (81)        // cmd: push data
#define XKCP_CMD_ACK        (82)        // cmd: ack
#define XKCP_CMD_WASK       (83)        // cmd: window probe (ask)
#define XKCP_CMD_WINS       (84)        // cmd: window size (tell)
#define XKCP_CMD_SYNC       (85)        // [XKCP] 注册/重连握手请求 (绕过 sn 窗口, 客户端发)
#define XKCP_CMD_SACK       (86)        // [XKCP] 注册/重连握手应答 (服务端发, 客户端 on_regist_rsp)
#define XKCP_CMD_RST        (87)        // [XKCP] 顶号复位 (其他设备登录, 服务端发给旧端)
#define XKCP_CMD_PING       (88)        // [XKCP] 保活心跳 (空闲自动发)
#define XKCP_CMD_PONG       (89)        // [XKCP] 心跳应答
#define XKCP_RTO_NDL        (30)        // no delay min rto
#define XKCP_RTO_MIN        (100)       // normal min rto
#define XKCP_RTO_DEF        (200)
#define XKCP_RTO_MAX        (60000)
#define XKCP_ASK_SEND       (1)         // need to send XKCP_CMD_WASK
#define XKCP_ASK_TELL       (2)         // need to send XKCP_CMD_WINS
#define XKCP_WND_SND        (128)       // 发送窗口
#define XKCP_WND_RCV        (128)       // must >= max fragment size
#define XKCP_MTU_DEF        (XKCP_LINK_MTU - XKCP_IPV4_HDR - XKCP_UDP_HDR - crypto_shorthash_BYTES)
#define XKCP_MSS            (XKCP_MTU_DEF - XKCP_HDR_LEN)   // 最大分片(编译期常量, setmtu 已取消)
#define XKCP_ACK_FAST       (3)
#define XKCP_INTERVAL       (100)
#define XKCP_DEADLINK       (20)
#define XKCP_THRESH_INIT    (2)
#define XKCP_THRESH_MIN     (2)
#define XKCP_PROBE_INIT     (5000)      // 7 secs to probe window size
#define XKCP_PROBE_LIMIT    (120000)    // up to 120 secs to probe window
#define XKCP_FASTACK_LIMIT  (5)         // max times to trigger fastack
#define XKCP_DEAD_TIMEOUT   (45000)
#define XKCP_PAYLOAD_MAX    (sizeof(struct XKCPTOKEN) + 32 + 16)


#define ASSERT(expr)                                         \
    do {                                                     \
        if (!(expr)) {                                       \
            fprintf(stderr,                                  \
                    "ASSERT FAILED: %s\n"                    \
                    "FILE: %s\n"                             \
                    "LINE: %d\n",                            \
                    #expr, __FILE__, __LINE__);              \
            abort();                                         \
        }                                                    \
    } while (0)


/**
 * @brief KCP 全局配置
 */
static struct XKCPCONF {
    uint8_t  x25519_pk[32];    // 服务端 x25519 公钥(sealedbox 收件)
    uint8_t  x25519_sk[32];    // 服务端 x25519 私钥(sealedbox 开封)
    uint8_t  ed25519_pk[32];   // 登录服 ed25519 公钥(验 token 签名)
    uint8_t  siphash_key[16];  // SipHash 信封 MAC 密钥(每出向数据报前置 8B tag)
    int32_t  rx_minrto;        // 最小 RTO
    int32_t  fastresend;       // 跨越多少次触发快速重传(0 = 关)
    int32_t  nocwnd;           // 关闭拥塞控制
    int32_t  logmask;          // 日志掩码(XKCP_LOG_*)
    uint32_t snd_wnd;          // 发送窗口(段)
    uint32_t rcv_wnd;          // 接收窗口(段)
    uint32_t nodelay;          // 极速模式开关
    uint32_t interval;         // flush 间隔(ms)
    uint32_t dead_timeout;     // 超时(ms)

    const xkcpops* ccops;      // 策略回调集
    void*          congest;    // 策略私有状态

    // 出向发送回调
    int (*output)(const uint8_t* buf, int len, struct XKCPCB *kcp);

    // 日志回调
    void (*writelog)(const char *log, struct XKCPCB *kcp, void *user);
} __conf_;


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


static inline uint8_t*
xkcp_encode64u(uint8_t *p, uint64_t l) {
#if XWORDS_BIG_ENDIAN || XWORDS_MUST_ALIGN
    *(uint8_t*)(p + 0) = (uint8_t)((l >>  0) & 0xff);
    *(uint8_t*)(p + 1) = (uint8_t)((l >>  8) & 0xff);
    *(uint8_t*)(p + 2) = (uint8_t)((l >> 16) & 0xff);
    *(uint8_t*)(p + 3) = (uint8_t)((l >> 24) & 0xff);
    *(uint8_t*)(p + 4) = (uint8_t)((l >> 32) & 0xff);
    *(uint8_t*)(p + 5) = (uint8_t)((l >> 40) & 0xff);
    *(uint8_t*)(p + 6) = (uint8_t)((l >> 48) & 0xff);
    *(uint8_t*)(p + 7) = (uint8_t)((l >> 56) & 0xff);
#else
    memcpy(p, &l, 8);
#endif
    p += 8;
    return p;
}


static inline const uint8_t*
xkcp_decode64u(const uint8_t* p, uint64_t* l) {
#if XWORDS_BIG_ENDIAN || XWORDS_MUST_ALIGN
    *l = *(const uint8_t*)(p + 7);
    *l = *(const uint8_t*)(p + 6) + (*l << 8);
    *l = *(const uint8_t*)(p + 5) + (*l << 8);
    *l = *(const uint8_t*)(p + 4) + (*l << 8);
    *l = *(const uint8_t*)(p + 3) + (*l << 8);
    *l = *(const uint8_t*)(p + 2) + (*l << 8);
    *l = *(const uint8_t*)(p + 1) + (*l << 8);
    *l = *(const uint8_t*)(p + 0) + (*l << 8);
#else 
    memcpy(l, p, 8);
#endif
    p += 8;
    return p;
}


// 返回 a、b 中较小者
static inline uint32_t
_xmin_(uint32_t a, uint32_t b) {
    return a <= b ? a : b;
}


// 返回 a、b 中较大者
static inline uint32_t
_xmax_(uint32_t a, uint32_t b) {
    return a >= b ? a : b;
}


// 把 middle 钳制到 [lower, upper]
static inline uint32_t
_xbound_(uint32_t lower, uint32_t middle, uint32_t upper) {
    return _xmin_(_xmax_(lower, middle), upper);
}


// 计算时间差 later - earlier; 经 int32 转换, 正确处理 32 位时钟回绕
static inline int32_t
_xtimediff(uint32_t later, uint32_t earlier) {
    return (int32_t)(later - earlier);
}


/**
 * @brief 分配一个新的 kcp 段(段头 + size 字节数据区)
 * @param size  数据区字节数
 * @return 新段指针
 */
static inline xkcpseg*
xkcp_segment_new(xkcpcb*, int size) {
    return (xkcpseg*)mi_malloc(sizeof(xkcpseg) + size);
}

/**
 * @brief 释放一个段
 * @param seg  待释放的段
 */
static inline void
xkcp_segment_delete(xkcpcb*, xkcpseg* seg) {
    mi_free(seg);
}


/**
 * @brief 判断某类日志当前是否需要输出
 * @param kcp   会话
 * @param mask  日志类别(XKCP_LOG_*)
 * @return 需要输出返回 1, 否则 0
 */
static int
xkcp_canlog(const xkcpcb* kcp, int mask) {
    if ((mask & __conf_.logmask) == 0 || __conf_.writelog == NULL) return 0;
    return 1;
}


/**
 * @brief 出向唯一收口: 前置 8B SipHash 信封后经 kcp->output 回调发出, 并刷新 last_snd_ms
 * @param kcp   会话
 * @param data  待发送的 KCP 数据报(不含信封)
 * @param size  数据报字节数(为 0 则直接返回)
 * @return output 回调的返回值; size 为 0 时返回 0
 */
static int
xkcp_output(xkcpcb* kcp, const uint8_t* data, int size) {
    ASSERT(kcp != NULL && __conf_.output != NULL);

    if (xkcp_canlog(kcp, XKCP_LOG_OUTPUT)) {
        xkcp_log(kcp, XKCP_LOG_OUTPUT, "[RO] %ld bytes", (long)size);
    }

    if (size == 0) {
        return 0;
    }

    crypto_shorthash(kcp->mac_buf, data, (size_t)size, __conf_.siphash_key);
    memcpy(kcp->mac_buf + crypto_shorthash_BYTES, data, (size_t)size);

    kcp->last_snd_ms = kcp->current;
    return __conf_.output(kcp->mac_buf, size + (int)crypto_shorthash_BYTES, kcp);
}


/**
 * @brief [XKCP] 发一个无 payload 的控制段(如 PING/PONG); 经 xkcp_output 自动带信封
 * @param kcp  会话
 * @param cmd  控制 cmd(XKCP_CMD_PING / PONG 等)
 */
static inline void
xkcp_output_ctrl(xkcpcb* kcp, uint32_t cmd, const uint8_t* payload, int plen) {
    uint8_t buf[XKCP_HDR_LEN + XKCP_PAYLOAD_MAX];
    uint8_t *p = buf;
    // conv
    p = xkcp_encode32u(p, kcp->conv);
    // cmd
    p = xkcp_encode8u(p, (uint8_t)cmd);
    // frg
    p = xkcp_encode8u(p, 0);
    // wnd
    p = xkcp_encode16u(p, (uint16_t)__conf_.rcv_wnd);
    // ts
    p = xkcp_encode32u(p, 0);
    // sn
    p = xkcp_encode32u(p, 0);
    // una
    p = xkcp_encode32u(p, kcp->rcv_nxt);
    // len
    p = xkcp_encode32u(p, 0);
    // payload
    if (plen > 0 && payload != NULL) {
        memcpy(p, payload, plen);
    }

    xkcp_output(kcp, buf, (int)(p - buf));
}


/**
 * @brief [XKCP] 构造 12B AEAD nonce = conv(4) | seq(4) | dir(1) | 0(3)
 * @param nonce  [out] 12 字节 nonce 缓冲
 * @param conv   会话号
 * @param seq    本端消息序号(每条消息递增, 保证同密钥下 nonce 唯一)
 * @param dir    方向字节(DIR_C2S=0 / DIR_S2C=1, 使双向 nonce 不相撞)
 */
static inline void
xkcp_make_nonce(uint8_t* nonce, uint32_t conv, uint32_t seq, uint8_t dir) {
    memset(nonce, 0, crypto_aead_chacha20poly1305_ietf_NPUBBYTES);
    memcpy(nonce + 0, &conv, sizeof(conv));
    memcpy(nonce + 4, &seq, sizeof(seq));
    nonce[8] = dir;
}



inline int
xkcp_ed25519_verify(const uint8_t* sig, const uint8_t* msg, size_t mlen, const uint8_t* pk) {
    return crypto_sign_verify_detached(sig, msg, (uint64_t)mlen, pk);
}



inline int
xkcp_sealedbox_encrypt(uint8_t* out, const uint8_t* in, size_t inlen, const uint8_t* pk) {
    return crypto_box_seal(out, in, (uint64_t)inlen, pk);
}


inline int
xkcp_sealedbox_decrypt(uint8_t* out, const uint8_t* in, size_t inlen, const uint8_t* pk, const uint8_t* sk) {
    return crypto_box_seal_open(out, in, (uint64_t)inlen, pk, sk);
}


/**
 * @brief 原始发送: 把数据按 mss 分片成段入 snd_queue(原生 KCP 逻辑; xkcp_send 在外层加 AEAD)
 * @param kcp     会话
 * @param buffer  待发送数据(可为 NULL, 仅占位)
 * @param len     数据长度
 * @return 成功返回已入队字节数(>=0); -1 参数错; -2 分片数超过接收窗口
 */
static int
xkcp_send_raw(xkcpcb* kcp, const uint8_t* buffer, int len) {
    ASSERT(XKCP_MSS > 0);

    xkcpseg *seg;
    int count, i;
    int sent = 0;
    
    if (len < 0) {
        return -1;
    }

    if (len <= XKCP_MSS) {
        count = 1;
    }
    else {
        count = (len + XKCP_MSS - 1) / XKCP_MSS;
    }

    if (count >= XKCP_WND_RCV) {
        return -2;
    }

    if (count == 0) {
        count = 1;
    }

    // fragment
    for (i = 0; i < count; i++) {
        int size = len > XKCP_MSS ? XKCP_MSS : len;
        seg = xkcp_segment_new(kcp, size);
        ASSERT(seg != NULL);

        if (buffer && len > 0) {
            memcpy(seg->data, buffer, size);
        }

        seg->len = size;
        seg->frg = count - i - 1;
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


/**
 * @brief 用一个 RTT 样本更新平滑 RTT(rx_srtt)与重传超时(rx_rto)
 * @param kcp  会话
 * @param rtt  本次测得的 RTT 样本(ms)
 */
static void 
xkcp_update_ack(xkcpcb* kcp, int32_t rtt) {
    int32_t rto = 0;
    if (kcp->rx_srtt == 0) {
        kcp->rx_srtt = rtt;
        kcp->rx_rttval = rtt / 2;
    } else {
        long delta = rtt - kcp->rx_srtt;
        if (delta < 0) {
            delta = -delta;
        }
        kcp->rx_rttval = (3 * kcp->rx_rttval + delta) / 4;
        kcp->rx_srtt = (7 * kcp->rx_srtt + rtt) / 8;
        if (kcp->rx_srtt < 1) {
            kcp->rx_srtt = 1;
        }
    }

    rto = kcp->rx_srtt + _xmax_(__conf_.interval, 4 * kcp->rx_rttval);
    kcp->rx_rto = _xbound_(__conf_.rx_minrto, rto, XKCP_RTO_MAX);
    if (__conf_.ccops && __conf_.ccops->on_rtt) {
        __conf_.ccops->on_rtt(kcp, rtt);
    }
}

/**
 * @brief 把 snd_una 推进到 snd_buf 首段的 sn(无未确认段时 = snd_nxt)
 * @param kcp  会话
 */
static void
xkcp_shrink_buf(xkcpcb *kcp) {
    struct XQUEUEHEAD *p = kcp->snd_buf.next;
    if (p != &kcp->snd_buf) {
        xkcpseg* seg = xqueue_entry(p, xkcpseg, node);
        kcp->snd_una = seg->sn;
    }   else {
        kcp->snd_una = kcp->snd_nxt;
    }
}

/**
 * @brief 收到某个 sn 的 ACK: 从 snd_buf 删掉该段(已确认)
 * @param kcp  会话
 * @param sn   被确认的段序号
 */
static void
xkcp_parse_ack(xkcpcb* kcp, uint32_t sn) {
    struct XQUEUEHEAD *p, *next;
    int32_t pkt_rtt;

    if (_xtimediff(sn, kcp->snd_una) < 0 || _xtimediff(sn, kcp->snd_nxt) >= 0)
        return;

    for (p = kcp->snd_buf.next; p != &kcp->snd_buf; p = next) {
        xkcpseg* seg = xqueue_entry(p, xkcpseg, node);
        next = p->next;
        if (sn == seg->sn) {
            kcp->ackedlen += seg->len;

            if (__conf_.ccops && __conf_.ccops->on_pkt_acked) {
                pkt_rtt = -1;
    
                if (_xtimediff(kcp->current, seg->ts) >= 0) {
                    pkt_rtt = _xtimediff(kcp->current, seg->ts);
                }

                __conf_.ccops->on_pkt_acked(kcp, seg->sn, seg->ts, seg->len, pkt_rtt, seg->xmit);
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

/**
 * @brief 按对端 una 批量确认: 删掉 snd_buf 里所有 sn < una 的段
 * @param kcp  会话
 * @param una  对端 una(此前的段对端都已收到)
 */
static void
xkcp_parse_una(xkcpcb* kcp, uint32_t una) {
    struct XQUEUEHEAD *p, *next;
    for (p = kcp->snd_buf.next; p != &kcp->snd_buf; p = next) {
        xkcpseg *seg = xqueue_entry(p, xkcpseg, node);
        next = p->next;
        if (_xtimediff(una, seg->sn) > 0) {
            kcp->ackedlen += seg->len;
            if (__conf_.ccops && __conf_.ccops->on_pkt_acked) {
                __conf_.ccops->on_pkt_acked(kcp, seg->sn, seg->ts,
                        seg->len, -1, seg->xmit);
            }

            xqueue_del(p);
            xkcp_segment_delete(kcp, seg);
            kcp->nsnd_buf--;
        } else {
            break;
        }
    }
}


/**
 * @brief 给 sn 之前仍未确认的段累计"被跨越"计数(fastack); 达 fastresend 阈值触发快速重传
 * @param kcp  会话
 * @param sn   本次 ACK 的最大 sn
 * @param ts   该 ACK 的时间戳(conserve 模式下参与判定)
 */
static void
xkcp_parse_fastack(xkcpcb* kcp, uint32_t sn, uint32_t ts) {
    struct XQUEUEHEAD *p, *next;

    if (_xtimediff(sn, kcp->snd_una) < 0 || _xtimediff(sn, kcp->snd_nxt) >= 0)
        return;

    for (p = kcp->snd_buf.next; p != &kcp->snd_buf; p = next) {
        xkcpseg* seg = xqueue_entry(p, xkcpseg, node);
        next = p->next;
        if (_xtimediff(sn, seg->sn) < 0) {
            break;
        }
        else if (sn != seg->sn) {
        #ifndef XKCP_FASTACK_CONSERVE
            seg->fastack++;
        #else
            if (_xtimediff(ts, seg->ts) >= 0) {
                seg->fastack++;
            }
        #endif
        }
    }
}


/**
 * @brief 记录一个待回的 ACK(sn,ts) 进 acklist, 等 flush 时统一发出
 * @param kcp  会话
 * @param sn   待确认的段序号
 * @param ts   段携带的时间戳(回 ACK 时带回, 供对端算 RTT)
 */
static void
xkcp_ack_push(xkcpcb* kcp, uint32_t sn, uint32_t ts) {
    uint32_t  newsize = kcp->ackcount + 1;
    uint32_t* ptr;

    if (newsize > kcp->ackblock) {
        uint32_t *acklist;
        uint32_t newblock;

        for (newblock = 8; newblock < newsize; newblock <<= 1);
        acklist = (uint32_t*)mi_malloc(newblock * sizeof(uint32_t) * 2);
        ASSERT(acklist != NULL);

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


/**
 * @brief 取出 acklist 里第 p 个待回 ACK 的 sn/ts
 * @param kcp  会话
 * @param p    acklist 下标
 * @param sn   [out] 该 ACK 的段序号
 * @param ts   [out] 该 ACK 的时间戳
 */
static void
xkcp_ack_get(const xkcpcb* kcp, int p, uint32_t* sn, uint32_t* ts) {
    if (sn) {
        sn[0] = kcp->acklist[p * 2 + 0];
    }

    if (ts) {
        ts[0] = kcp->acklist[p * 2 + 1];
    }
}


/**
 * @brief [XKCP] 数据 cmd(PUSH/ACK/WASK/WINS)的公共窗口处理: 更新对端窗口 + 按 una 确认 + 收缩 snd_buf
 * @param kcp  会话
 * @param wnd  对端通告的接收窗口
 * @param una  对端 una
 */
static inline void
xkcp_input_window(xkcpcb* kcp, uint16_t wnd, uint32_t una) {
    kcp->rmt_wnd = wnd;
    xkcp_parse_una(kcp, una);
    xkcp_shrink_buf(kcp);
}


/**
 * @brief [XKCP] 服务端收 REGIST_REQ: 传输层去重(同 id 重发→重发缓存 RSP), 新握手才上抛 on_regist 回调并缓存/发送 RSP
 * @param kcp   会话
 * @param data  REGIST_REQ payload(头部 XKCP_REGIST_ID_LEN 字节为握手 id)
 * @param len   payload 长度
 */
static void
xkcp_on_sync(xkcpcb* kcp, const uint8_t* data, int len) {
    // Step 1, 检查 data 长度
    if (len != XKCP_PAYLOAD_MAX) {
        return;
    }

    if (kcp->auth > XKCP_FASTACK_LIMIT) {
        kcp->state = (uint32_t)-1;
        return;
    }
        
    // Step 2, 使用 sealedbox 私钥进行解密
    struct XKCPTOKEN token;
    memset(&token, 0, sizeof(token));
    uint8_t plain[sizeof(token)];
    if (xkcp_sealedbox_decrypt(plain, data, len, __conf_.x25519_pk, __conf_.x25519_sk) != 0) {
        return;
    }

    // Step 3, 校验 Token 签名
    const uint8_t* p = plain;
    p = xkcp_decode64u(p, &token.expire);
    if (token.expire < time(NULL)) {
        return;
    }

    p = xkcp_decode32u(p, &token.conv);
    if (token.conv != kcp->conv) {
        return;
    }

    p = xkcp_decode32u(p, &token.gen);
    if (token.gen == 0 || kcp->gen > token.gen) {
        return;
    }

    memcpy(&token.peer_pk, p, sizeof(token.peer_pk));
    p += sizeof(token.peer_pk);

    if (xkcp_ed25519_verify(p, plain, p - plain, __conf_.ed25519_pk) != 0) {
        return;
    }

    if (kcp->gen < token.gen) {
        if (kcp->auth > 0) {
            xkcp_reset(kcp);
        }

        // Step 4, 生成密钥对
        xkcp_x25519_keygen(kcp);

        // Step 5, 通过 token.peer_pk 生成服务端 key
        if (xkcp_kx_server(kcp, token.peer_pk) != 0) {
            return;
        }
        kcp->gen = token.gen;
    }

    // Step 6, 回发生成的公钥对给对端
    xkcp_output_ctrl(kcp, XKCP_CMD_SACK, kcp->eph_pk, sizeof(kcp->eph_pk));
    ++kcp->auth;
}

/**
 * @brief [XKCP] 客户端收 REGIST_RSP: 上抛 on_regist_rsp 回调(取服务端公钥、算密钥、置 authed)
 * @param kcp   会话
 * @param data  REGIST_RSP payload
 * @param len   payload 长度
 */
static inline void
xkcp_on_sack(xkcpcb *kcp, const uint8_t *data, int len) {
    if (data == NULL || len != 32 || xkcp_kx_client(kcp, data) < 0) {
        return;
    }

    kcp->auth = 1;
}

/**
 * @brief [XKCP] 客户端收 RST(被顶号): 上抛 on_rst 回调
 * @param kcp   会话
 * @param data  RST payload
 * @param len   payload 长度
 */
static inline void
xkcp_on_rst(xkcpcb *kcp, const uint8_t *data, int len) {
    kcp->state = (uint32_t)-1;
}


/**
 * @brief [XKCP] 收 PING: 立即回 PONG
 * @param kcp  会话
 */
static inline void
xkcp_on_ping(xkcpcb *kcp) {
    if (kcp->auth == 0) {
        return;
    }

    xkcp_output_ctrl(kcp, XKCP_CMD_PONG, NULL, 0);
}

/**
 * @brief [XKCP] 收 PONG: 无需处理(last_rcv_ms 已在 xkcp_input 入口刷新)
 * @param kcp  会话
 */
static inline void
xkcp_on_pong(xkcpcb* kcp) {
    if (kcp->auth == 0) {
        return;
    }
}

/**
 * @brief 收 WASK(对端窗口探测): 标记下次 flush 回 WINS 告知本端窗口
 * @param kcp  会话
 */
static inline void
xkcp_on_wask(xkcpcb* kcp) {
    if (kcp->auth == 0) {
        return;
    }

    kcp->probe |= XKCP_ASK_TELL;
    if (xkcp_canlog(kcp, XKCP_LOG_IN_PROBE)) {
        xkcp_log(kcp, XKCP_LOG_IN_PROBE, "input probe");
    }
}

/**
 * @brief 收 WINS(对端窗口通告): 窗口已在 xkcp_input_window 更新, 此处仅记录日志
 * @param kcp  会话
 * @param wnd  对端通告的接收窗口
 */
static inline void
xkcp_on_wins(xkcpcb *kcp, uint16_t wnd) {
    if (kcp->auth == 0) {
        return;
    }

    if (xkcp_canlog(kcp, XKCP_LOG_IN_WINS)) {
        xkcp_log(kcp, XKCP_LOG_IN_WINS, "input wins: %lu", (unsigned long)wnd);
    }
}


/**
 * @brief 把段头(conv/cmd/frg/wnd/ts/sn/una/len, 共 24B)编码进 ptr
 * @param ptr  写入位置
 * @param seg  源段
 * @return 写入后的新位置(ptr + XKCP_OVERHEAD)
 */
static inline uint8_t*
xkcp_encode_seg(uint8_t* ptr, const xkcpseg* seg) {
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

/**
 * @brief 计算本端接收窗口剩余可用段数(用于在段头通告 wnd)
 * @param kcp  会话
 * @return rcv_wnd - nrcv_que; 已满则返回 0
 */
static inline int
xkcp_wnd_unused(const xkcpcb* kcp) {
    if (kcp->nrcv_que < __conf_.rcv_wnd) {
        return __conf_.rcv_wnd - kcp->nrcv_que;
    }
    return 0;
}


void
xkcp_log(xkcpcb* kcp, int mask, const char* fmt, ...) {
    char buffer[1024];
    va_list argptr;
    if ((mask & __conf_.logmask) == 0 || __conf_.writelog == 0) {
        return;
    }

    va_start(argptr, fmt);
    vsprintf(buffer, fmt, argptr);
    va_end(argptr);
    __conf_.writelog(buffer, kcp, kcp->user);
}


void
xkcp_init(
    int (*output)(const uint8_t* buf, int len, struct XKCPCB *kcp),
    const uint8_t* x25519_pk,
    const uint8_t* x25519_sk,
    const uint8_t* ed25519_pk,
    const uint8_t* siphash_key,
    uint32_t snd_wnd,
    uint32_t rcv_wnd,
    uint32_t interval,
    int32_t  fastresend,
    uint32_t dead_timeout
) {
    static int inited = 0;

    if (inited != 0) {
        return;
    }

    inited = 1;
    ASSERT(output != NULL && x25519_pk != NULL && x25519_sk != NULL && siphash_key != NULL);
    memset(&__conf_, 0, sizeof(__conf_));

    // 密钥: 定长直接拷入
    memcpy(__conf_.x25519_pk,   x25519_pk,   sizeof(__conf_.x25519_pk));
    memcpy(__conf_.x25519_sk,   x25519_sk,   sizeof(__conf_.x25519_sk));
    if (ed25519_pk != NULL) {
        memcpy(__conf_.ed25519_pk,  ed25519_pk,  sizeof(__conf_.ed25519_pk));
    }
    memcpy(__conf_.siphash_key, siphash_key, sizeof(__conf_.siphash_key));

    // 调参: 传 0 用内置默认, 非 0 用传入值
    __conf_.snd_wnd      = snd_wnd      ? snd_wnd      : XKCP_WND_SND;
    __conf_.rcv_wnd      = rcv_wnd      ? rcv_wnd      : XKCP_WND_RCV;
    __conf_.interval     = interval     ? interval     : XKCP_INTERVAL;
    __conf_.fastresend   = fastresend   ? fastresend   : XKCP_ACK_FAST;
    __conf_.dead_timeout = dead_timeout ? dead_timeout : XKCP_DEAD_TIMEOUT;

    __conf_.nodelay   = 1;
    __conf_.nocwnd    = 1;
    __conf_.rx_minrto = XKCP_RTO_MIN;
}


void
xkcp_x25519_keygen(xkcpcb* kcp) {
    crypto_kx_keypair(kcp->eph_pk, kcp->eph_sk);
}


int
xkcp_kx_server(xkcpcb* kcp, const uint8_t* client_pk) {
    uint8_t sk[crypto_kx_SECRETKEYBYTES];
    if (crypto_kx_server_session_keys(kcp->rx_key, kcp->tx_key, kcp->eph_pk, kcp->eph_sk, client_pk) != 0) {
        return -1;
    }
        
    kcp->snd_dir = 1;  // S2C
    kcp->rcv_dir = 0;  // C2S
    kcp->snd_seq = 0;
    kcp->rcv_seq = 0;

    return 0;
}


int
xkcp_kx_client(xkcpcb* kcp, const uint8_t* server_pk) {
    if (crypto_kx_client_session_keys(kcp->rx_key, kcp->tx_key, kcp->eph_pk, kcp->eph_sk, server_pk) != 0) {
        return -1;
    }

    kcp->snd_dir = 0;  // C2S
    kcp->rcv_dir = 1;  // S2C
    kcp->snd_seq = 0;
    kcp->rcv_seq = 0;

    return 0;
}


int
xkcp_sync(xkcpcb* kcp, const uint8_t* token, int len) {
    if (len != XKCP_PAYLOAD_MAX) {
        return -1;
    }

    xkcp_output_ctrl(kcp, XKCP_CMD_SYNC, token, len);
    return 0;
}


xkcpcb*
xkcp_create(uint32_t conv, void* user) {
    xkcpcb *kcp = (xkcpcb*)mi_malloc(sizeof(struct XKCPCB));
    ASSERT(kcp != NULL);

    kcp->conv = conv;
    kcp->user = user;
    kcp->snd_una = 0;
    kcp->snd_nxt = 0;
    kcp->rcv_nxt = 0;
    kcp->ts_probe = 0;
    kcp->probe_wait = 0;
    kcp->rmt_wnd = XKCP_WND_RCV;
    kcp->cwnd = 0;
    kcp->incr = 0;
    kcp->probe = 0;

    kcp->buffer = (uint8_t*)mi_malloc((XKCP_MTU_DEF + XKCP_HDR_LEN) * 3);
    ASSERT(kcp->buffer != NULL);

    // [XKCP] 出向信封暂存: 比 buffer 多 8B 放前置 MAC
    kcp->mac_buf = (uint8_t*)mi_malloc((XKCP_MTU_DEF + XKCP_HDR_LEN) * 2 + crypto_shorthash_BYTES);
    ASSERT(kcp->mac_buf != NULL);

    xqueue_init(&kcp->snd_queue);
    xqueue_init(&kcp->rcv_queue);
    xqueue_init(&kcp->snd_buf);
    xqueue_init(&kcp->rcv_buf);
    kcp->nrcv_buf     = 0;
    kcp->nsnd_buf     = 0;
    kcp->nrcv_que     = 0;
    kcp->nsnd_que     = 0;
    kcp->state        = 0;
    kcp->acklist      = NULL;
    kcp->ackblock     = 0;
    kcp->ackcount     = 0;
    kcp->ackedlen     = 0;
    kcp->rx_srtt      = 0;
    kcp->rx_rttval    = 0;
    kcp->rx_rto       = XKCP_RTO_DEF;
    kcp->current      = 0;
    kcp->ts_flush     = XKCP_INTERVAL;
    kcp->updated      = 0;
    kcp->ssthresh     = XKCP_THRESH_INIT;
    kcp->xmit         = 0;
    kcp->last_snd_ms  = 0;
    kcp->last_rcv_ms  = 0;
    kcp->state        = 0;
    kcp->snd_seq = 0;
    kcp->rcv_seq = 0;
    kcp->snd_dir      = 0;
    kcp->rcv_dir      = 0;
    kcp->gen = 0;

    return kcp;
}


void
xkcp_release(xkcpcb *kcp) {
    xkcpseg *seg;
    
    if (kcp == NULL) {
        return;
    }

    while (!xqueue_is_empty(&kcp->snd_buf)) {
        seg = xqueue_entry(kcp->snd_buf.next, xkcpseg, node);
        xqueue_del(&seg->node);
        xkcp_segment_delete(kcp, seg);
    }

    while (!xqueue_is_empty(&kcp->rcv_buf)) {
        seg = xqueue_entry(kcp->rcv_buf.next, xkcpseg, node);
        xqueue_del(&seg->node);
        xkcp_segment_delete(kcp, seg);
    }

    while (!xqueue_is_empty(&kcp->snd_queue)) {
        seg = xqueue_entry(kcp->snd_queue.next, xkcpseg, node);
        xqueue_del(&seg->node);
        xkcp_segment_delete(kcp, seg);
    }

    while (!xqueue_is_empty(&kcp->rcv_queue)) {
        seg = xqueue_entry(kcp->rcv_queue.next, xkcpseg, node);
        xqueue_del(&seg->node);
        xkcp_segment_delete(kcp, seg);
    }

    if (kcp->buffer) {
        mi_free(kcp->buffer);
    }

    if (kcp->mac_buf) {
        mi_free(kcp->mac_buf);
    }

    if (kcp->acklist) {
        mi_free(kcp->acklist);
    }

    mi_free(kcp);
}


void
xkcp_reset(xkcpcb* kcp) {
    xkcpseg *seg;

    if (kcp == NULL) {
        return;
    }

    while (!xqueue_is_empty(&kcp->snd_buf)) {
        seg = xqueue_entry(kcp->snd_buf.next, xkcpseg, node);
        xqueue_del(&seg->node);
        xkcp_segment_delete(kcp, seg);
    }

    while (!xqueue_is_empty(&kcp->rcv_buf)) {
        seg = xqueue_entry(kcp->rcv_buf.next, xkcpseg, node);
        xqueue_del(&seg->node);
        xkcp_segment_delete(kcp, seg);
    }

    while (!xqueue_is_empty(&kcp->snd_queue)) {
        seg = xqueue_entry(kcp->snd_queue.next, xkcpseg, node);
        xqueue_del(&seg->node);
        xkcp_segment_delete(kcp, seg);
    }

    while (!xqueue_is_empty(&kcp->rcv_queue)) {
        seg = xqueue_entry(kcp->rcv_queue.next, xkcpseg, node);
        xqueue_del(&seg->node);
        xkcp_segment_delete(kcp, seg);
    }

    if (kcp->acklist) {
        mi_free(kcp->acklist);
    }

    kcp->acklist     = NULL;
    kcp->ackblock    = 0;
    kcp->ackcount    = 0;
    kcp->ackedlen    = 0;
    kcp->snd_una     = 0;
    kcp->snd_nxt     = 0;
    kcp->rcv_nxt     = 0;
    kcp->nrcv_buf    = 0;
    kcp->nsnd_buf    = 0;
    kcp->nrcv_que    = 0;
    kcp->nsnd_que    = 0;
    kcp->state       = 0;
    kcp->ts_probe    = 0;
    kcp->probe_wait  = 0;
    kcp->probe       = 0;
    kcp->rmt_wnd     = XKCP_WND_RCV;
    kcp->cwnd        = 0;
    kcp->incr        = 0;
    kcp->ssthresh    = XKCP_THRESH_INIT;
    kcp->rx_srtt     = 0;
    kcp->rx_rttval   = 0;
    kcp->rx_rto      = XKCP_RTO_DEF;
    kcp->xmit        = 0;
    kcp->last_snd_ms = kcp->current;
    kcp->last_rcv_ms = kcp->current;
    kcp->state       = 0;
    kcp->snd_seq     = 0;
    kcp->rcv_seq     = 0;
    kcp->gen         = 0;
}


int
xkcp_recv(xkcpcb* kcp, uint8_t* buffer, int len) {
    ASSERT(kcp != NULL);

    struct XQUEUEHEAD *p;
    int recover = 0;
    uint8_t *const savedbuf = buffer;

    int peeksize = xkcp_peeksize(kcp);
    if (peeksize < 0)  {
        return peeksize;
    }

    if (peeksize > len) {
        return -3;
    }

    if (kcp->nrcv_que >= __conf_.rcv_wnd) {
        // 拥塞控制触发流量控制
        recover = 1;
    }

    // 组包
    xkcpseg* seg;
    for (len = 0, p = kcp->rcv_queue.next; p != &kcp->rcv_queue; ) {
        int fragment;
        seg = xqueue_entry(p, xkcpseg, node);
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

    ASSERT(len == peeksize);

    // 移除已到达的数据
    while (!xqueue_is_empty(&kcp->rcv_buf)) {
        seg = xqueue_entry(kcp->rcv_buf.next, xkcpseg, node);
        if (seg->sn == kcp->rcv_nxt && kcp->nrcv_que < __conf_.rcv_wnd) {
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
    if (kcp->nrcv_que < __conf_.rcv_wnd && recover) {
        // 告知对端当前接收窗口
        kcp->probe |= XKCP_ASK_TELL;
    }

    // AEAD 解密: 整条消息在用户 buffer 原地解密; 认证失败 = 篡改/错乱 → 返回 -4
    if (kcp->auth > 0 && savedbuf != NULL && len >= (int)crypto_aead_chacha20poly1305_ietf_ABYTES) {
        uint8_t nonce[crypto_aead_chacha20poly1305_ietf_NPUBBYTES];
        int plen = len - (int)crypto_aead_chacha20poly1305_ietf_ABYTES;
        xkcp_make_nonce(nonce, kcp->conv, ++kcp->rcv_seq, kcp->rcv_dir);
        if (crypto_aead_chacha20poly1305_ietf_decrypt_detached((uint8_t*)savedbuf, NULL, (const uint8_t*)savedbuf, (uint64_t)plen, (const uint8_t*)(savedbuf + plen), NULL, 0, nonce, kcp->rx_key) != 0) {
            return -4;
        }
        len = plen;
    }

    return len;
}


int
xkcp_peeksize(const xkcpcb* kcp) {
    ASSERT(kcp != NULL);

    struct XQUEUEHEAD *p;
    xkcpseg *seg;
    int length = 0;

    if (xqueue_is_empty(&kcp->rcv_queue)) {
        return -1;
    }

    seg = xqueue_entry(kcp->rcv_queue.next, xkcpseg, node);
    if (seg->frg == 0) {
        return seg->len;
    }

    if (kcp->nrcv_que < seg->frg + 1) {
        return -1;
    }

    for (p = kcp->rcv_queue.next; p != &kcp->rcv_queue; p = p->next) {
        seg = xqueue_entry(p, xkcpseg, node);
        length += seg->len;
        if (seg->frg == 0) {
            break;
        }
    }

    return length;
}


int 
xkcp_send(xkcpcb* kcp, const uint8_t* buffer, int len) {
    if (kcp->auth > 0 && buffer && len > 0) {
        int clen = len + (int)crypto_aead_chacha20poly1305_ietf_ABYTES;
        uint8_t nonce[crypto_aead_chacha20poly1305_ietf_NPUBBYTES];
        uint8_t *ct = (uint8_t*)mi_malloc(clen);

        if (ct == NULL) {
            return -1;
        }
    
        xkcp_make_nonce(nonce, kcp->conv, ++kcp->snd_seq, kcp->snd_dir);
        if (crypto_aead_chacha20poly1305_ietf_encrypt_detached((uint8_t*)ct, (uint8_t*)(ct + len), NULL, (const uint8_t*)buffer, (uint64_t)len, NULL, 0, NULL, nonce, kcp->tx_key) < 0) {
            mi_free(ct);
            return -1;
        }

        int ret = xkcp_send_raw(kcp, ct, clen);
        mi_free(ct);
        return ret;
    }

    return xkcp_send_raw(kcp, buffer, len);
}


/**
 * @brief 收到一个数据段: 按 sn 去重+排序插入 rcv_buf, 把从 rcv_nxt 起连续的段搬进 rcv_queue
 * @param kcp     会话
 * @param newseg  新到达的数据段(函数接管其所有权: 入队或删除)
 */
void
xkcp_parse_data(xkcpcb* kcp, xkcpseg* newseg) {
    struct XQUEUEHEAD *p, *prev;
    uint32_t sn = newseg->sn;
    int repeat = 0;
    
    if (_xtimediff(sn, kcp->rcv_nxt + __conf_.rcv_wnd) >= 0 ||
        _xtimediff(sn, kcp->rcv_nxt) < 0) {
        xkcp_segment_delete(kcp, newseg);
        return;
    }

    for (p = kcp->rcv_buf.prev; p != &kcp->rcv_buf; p = prev) {
        xkcpseg *seg = xqueue_entry(p, xkcpseg, node);
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

    // move available data from rcv_buf -> rcv_queue
    while (!xqueue_is_empty(&kcp->rcv_buf)) {
        xkcpseg *seg = xqueue_entry(kcp->rcv_buf.next, xkcpseg, node);
        if (seg->sn == kcp->rcv_nxt && kcp->nrcv_que < __conf_.rcv_wnd) {
            xqueue_del(&seg->node);
            kcp->nrcv_buf--;
            xqueue_add_tail(&seg->node, &kcp->rcv_queue);
            kcp->nrcv_que++;
            kcp->rcv_nxt++;
        } else {
            break;
        }
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

    if (data == NULL || size < crypto_shorthash_BYTES + XKCP_HDR_LEN) {
        return -1;
    }

    data += crypto_shorthash_BYTES;
    size -= crypto_shorthash_BYTES;

    kcp->last_rcv_ms = kcp->current;

    while (1) {
        uint32_t ts, sn, len, una, conv;
        uint16_t wnd;
        uint8_t cmd, frg;
        xkcpseg *seg;

        if (size < XKCP_HDR_LEN) {
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

        size -= XKCP_HDR_LEN;

        if ((long)size < (long)len || (int)len < 0) {
            return -2;
        }

        switch (cmd) {
        case XKCP_CMD_SYNC:
            xkcp_on_sync(kcp, data, (int)len);
            break;

        case XKCP_CMD_SACK:
            xkcp_on_sack(kcp, data, (int)len);
            break;

        case XKCP_CMD_RST:
            xkcp_on_rst(kcp, data, (int)len);
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
            if (kcp->auth == 0) {
                return;
            }

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
            } else {
                if (_xtimediff(sn, maxack) > 0) {
                #ifndef XKCP_FASTACK_CONSERVE
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
            if (kcp->auth == 0) {
                return;
            }

            xkcp_input_window(kcp, wnd, una);
            if (xkcp_canlog(kcp, XKCP_LOG_IN_DATA)) {
                xkcp_log(kcp, XKCP_LOG_IN_DATA, 
                    "input psh: sn=%lu ts=%lu", (unsigned long)sn, (unsigned long)ts);
            }

            if (_xtimediff(sn, kcp->rcv_nxt + __conf_.rcv_wnd) < 0) {
                xkcp_ack_push(kcp, sn, ts);
                if (_xtimediff(sn, kcp->rcv_nxt) >= 0) {
                    seg = xkcp_segment_new(kcp, len);
                    ASSERT(seg != NULL);
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
        if (__conf_.ccops && __conf_.ccops->on_ack) {
            __conf_.ccops->on_ack(kcp, acked_segs, kcp->ackedlen, prior_in_flight);
        }
        else {
            if (kcp->cwnd < kcp->rmt_wnd) {
                uint32_t mss = XKCP_MSS;
                if (kcp->cwnd < kcp->ssthresh) {
                    kcp->cwnd++;
                    kcp->incr += mss;
                }   else {
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


void
xkcp_flush(xkcpcb* kcp) {
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
    xkcpseg seg;

    // 'xkcp_update' hasn't been called yet. 
    if (kcp->updated == 0) {
        return;
    }

    if (__conf_.ccops && __conf_.ccops->on_tick) {
        __conf_.ccops->on_tick(kcp);
    }

    if (__conf_.ccops && __conf_.ccops->pacing_rate) {
        pacing_budget = (int32_t)__conf_.ccops->pacing_rate(kcp);
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
        if (size + (int)XKCP_HDR_LEN > (int)XKCP_MTU_DEF) {
            xkcp_output(kcp, buffer, size);
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
    }   else {
        kcp->ts_probe = 0;
        kcp->probe_wait = 0;
    }

    // flush window probing commands
    if (kcp->probe & XKCP_ASK_SEND) {
        seg.cmd = XKCP_CMD_WASK;
        size = (int)(ptr - buffer);
        if (size + (int)XKCP_HDR_LEN > (int)XKCP_MTU_DEF) {
            xkcp_output(kcp, buffer, size);
            ptr = buffer;
        }
        ptr = xkcp_encode_seg(ptr, &seg);
    }

    // flush window probing commands
    if (kcp->probe & XKCP_ASK_TELL) {
        seg.cmd = XKCP_CMD_WINS;
        size = (int)(ptr - buffer);
        if (size + (int)XKCP_HDR_LEN > (int)XKCP_MTU_DEF) {
            xkcp_output(kcp, buffer, size);
            ptr = buffer;
        }
        ptr = xkcp_encode_seg(ptr, &seg);
    }

    kcp->probe = 0;

    // calculate window size
    cwnd = _xmin_(__conf_.snd_wnd, kcp->rmt_wnd);
    if (__conf_.ccops != NULL || __conf_.nocwnd == 0) {
        cwnd = _xmin_(kcp->cwnd, cwnd);
    }

    // move data from snd_queue to snd_buf
    while (_xtimediff(kcp->snd_nxt, kcp->snd_una + cwnd) < 0) {
        xkcpseg *newseg;
        if (xqueue_is_empty(&kcp->snd_queue)) {
            break;
        }

        newseg = xqueue_entry(kcp->snd_queue.next, xkcpseg, node);

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
    if (__conf_.ccops && __conf_.ccops->on_app_limited) {
        if (xqueue_is_empty(&kcp->snd_queue)) {
            eff_cwnd = _xmin_(__conf_.snd_wnd, kcp->rmt_wnd);
            eff_cwnd = _xmin_(kcp->cwnd, eff_cwnd);
            cur_inflight = kcp->nsnd_buf;
            if (cur_inflight < eff_cwnd) {
                __conf_.ccops->on_app_limited(kcp, cur_inflight);
            }
        }
    }

    // calculate resent
    resent = (__conf_.fastresend > 0) ? (uint32_t)__conf_.fastresend : 0xffffffff;
    rtomin = (__conf_.nodelay == 0) ? (kcp->rx_rto >> 3) : 0;

    // flush data segments
    for (p = kcp->snd_buf.next; p != &kcp->snd_buf; p = p->next) {
        xkcpseg *segment = xqueue_entry(p, xkcpseg, node);
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
            if (__conf_.nodelay == 0) {
                segment->rto += _xmax_(segment->rto, (uint32_t)kcp->rx_rto);
            }   else {
                int32_t step = (__conf_.nodelay < 2)? 
                    ((int32_t)(segment->rto)) : kcp->rx_rto;
                segment->rto += step / 2;
            }
            segment->resendts = current + segment->rto;
            lost = 1;
        }
        else if (segment->fastack >= resent) {
            if (segment->xmit <= XKCP_FASTACK_LIMIT) {
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

            if (__conf_.ccops && __conf_.ccops->on_pkt_sent) {
                __conf_.ccops->on_pkt_sent(kcp, segment->sn, current, segment->len, kcp->nsnd_buf, segment->xmit);
            }

            size = (int)(ptr - buffer);
            need = XKCP_HDR_LEN + segment->len;

            if (size + need > (int)XKCP_MTU_DEF) {
                xkcp_output(kcp, buffer, size);
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

            if (segment->xmit >= XKCP_DEADLINK) {
                kcp->state = (uint32_t)-1;
            }
        }
    }

    // flash remaining segments
    size = (int)(ptr - buffer);
    if (size > 0) {
        xkcp_output(kcp, buffer, size);
    }

    // update ssthresh
    if (change) {
        if (__conf_.ccops && __conf_.ccops->on_fast_retransmit) {
            __conf_.ccops->on_fast_retransmit(kcp, (uint32_t)change, 
                    kcp->nsnd_buf, prior_cwnd);
        }
        else {
            uint32_t inflight = kcp->snd_nxt - kcp->snd_una;
            kcp->ssthresh = inflight / 2;
            if (kcp->ssthresh < XKCP_THRESH_MIN)
                kcp->ssthresh = XKCP_THRESH_MIN;
            kcp->cwnd = kcp->ssthresh + resent;
            kcp->incr = kcp->cwnd * XKCP_MSS;
        }
    }

    if (lost) {
        if (__conf_.ccops && __conf_.ccops->on_timeout) {
            __conf_.ccops->on_timeout(kcp, prior_cwnd);
        }
        else {
            kcp->ssthresh = prior_cwnd / 2;
            if (kcp->ssthresh < XKCP_THRESH_MIN) {
                kcp->ssthresh = XKCP_THRESH_MIN;
            }

            kcp->cwnd = 1;
            kcp->incr = XKCP_MSS;
        }
    }

    if (kcp->cwnd < 1) {
        kcp->cwnd = 1;
        kcp->incr = XKCP_MSS;
    }
}


int
xkcp_update(xkcpcb* kcp, uint32_t current) {
    int32_t slap;

    if (kcp->state != 0) {
        return -1;
    }

    kcp->current = current;
    uint32_t ping_interval = __conf_.dead_timeout / 3;

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
        kcp->ts_flush += __conf_.interval;
        if (_xtimediff(kcp->current, kcp->ts_flush) >= 0) {
            kcp->ts_flush = kcp->current + __conf_.interval;
        }
        xkcp_flush(kcp);
    }

    // 保活/超时(用 current 这个 32bit 时钟; 阈值 0 = 关闭)
    if (__conf_.dead_timeout > 0 && kcp->auth > 0 && _xtimediff(kcp->current, kcp->last_rcv_ms) > (int32_t)__conf_.dead_timeout) {
        kcp->state = (uint32_t)-1;
        return -1;
    }
    else if (ping_interval > 0 && kcp->auth > 0 && _xtimediff(kcp->current, kcp->last_snd_ms) > (int32_t)ping_interval) {
        xkcp_output_ctrl(kcp, XKCP_CMD_PING, NULL, 0);
    }

    return 0;
}


uint32_t
xkcp_check(const xkcpcb* kcp, uint32_t current) {
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
        const xkcpseg *seg = xqueue_entry(p, const xkcpseg, node);
        int32_t diff = _xtimediff(seg->resendts, current);
        if (diff <= 0) {
            return current;
        }
        if (diff < tm_packet) tm_packet = diff;
    }

    minimal = (uint32_t)(tm_packet < tm_flush ? tm_packet : tm_flush);
    if (minimal >= __conf_.interval) minimal = __conf_.interval;

    return current + minimal;
}


/**
 * @brief 设置内部 flush 帧间隔(钳制到 [10, 5000] ms)
 * @param kcp       会话
 * @param interval  帧间隔(ms)
 */
void
xkcp_interval(xkcpcb *kcp, int interval) {
    (void)kcp;   // 配置已全局化到 __conf_, kcp 仅为兼容旧签名
    if (interval > 5000) {
        interval = 5000;
    }
    else if (interval < 10) {
        interval = 10;
    }

    __conf_.interval = interval;
}


int
xkcp_waitsnd(const xkcpcb *kcp) {
    return kcp->nsnd_buf + kcp->nsnd_que;
}


inline uint32_t
xkcp_getconv(const void *raw) {
    uint32_t conv;
    xkcp_decode32u((const uint8_t*)raw + crypto_shorthash_BYTES, &conv);
    return conv;
}


int
xkcp_setcc(xkcpcb *kcp, const struct XKCPOPS *ops) {
    ASSERT(kcp != NULL);

    if (__conf_.ccops && __conf_.ccops->release) {
        __conf_.ccops->release(kcp);
    }

    __conf_.congest = NULL;
    __conf_.ccops = ops;
    if (ops) {
        if (ops->init && ops->init(kcp) < 0) {
            __conf_.ccops = NULL;
            __conf_.congest = NULL;

            if (kcp->cwnd < 1) {
                kcp->cwnd = 1;
            }

            kcp->incr = kcp->cwnd * XKCP_MSS;
            return -1;
        }
    } else {
        if (kcp->cwnd < 1) {
            kcp->cwnd = 1;
        }

        kcp->incr = kcp->cwnd * XKCP_MSS;
        if (kcp->incr < XKCP_MSS) {
            kcp->incr = XKCP_MSS;
        }
    }

    return 0;
}