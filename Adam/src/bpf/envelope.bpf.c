// envelope.bpf.c - XDP 层 DoS 过滤
//
// 在网卡驱动层校验 UDP payload 前 8 字节的 SipHash-2-4 envelope MAC.
//   - 通过 → XDP_PASS, 包继续走 sk_reuseport 分流 + userland 处理
//   - 不过 → XDP_DROP, 包根本不进内核 UDP stack, 不占任何 socket 资源
//
// wire 布局 (与 src/utils/cryptor.cpp 和 src/core/buffer.hpp 严格对齐):
//   UDP payload = [MAC 8B (LE)][KCP frame: header 24B + segment data 0~1200B]
//                 ↑ tag                    ↑ siphash24() 的输入 (前 24 字节)
//
// **MAC 只覆盖 KCP frame 前 24 字节** (= KCP wire header):
//   - DoS 防御: 攻击者必须猜对 conv/sn/cmd/... 才能生成合法 MAC
//   - XDP 端: 3 个 SipHash block 全展开, verifier 秒过, 无 tail
//   - sender 端: hash min(frame_len, 24) 字节, 所有 KCP segment >= 24B 永远能 hash 满
//   - payload 部分的完整性后期由 ChaCha20-Poly1305 保护
//
// kernel 要求:
//   - Linux >= 5.0 (XDP 基础, 无需 bpf_loop)
//   - 网卡驱动支持 XDP native mode 最佳; generic mode 也能跑只是慢

#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <linux/in.h>
#include <bpf/bpf_endian.h>
#include <bpf/bpf_helpers.h>


// =============================================================================
//                             配置常量 (与 C++ 端 adam.in.hpp 同步)
// =============================================================================

#define ENVELOPE_MAC_LEN       8
#define ENVELOPE_MAC_HASH_LEN  24
#define KCP_HDR_LEN            24
#define KCP_MTU                1392


// =============================================================================
//                             BPF maps
// =============================================================================

// 密钥表
#define ENVELOPE_KEY_SLOTS 256

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, ENVELOPE_KEY_SLOTS);
    __type(key, __u32);
    __type(value, __u8[16]);
} envelope_key SEC(".maps");


// 活跃会话的 conv 集合, 由用户态在建/摘会话时维护(见 EnvelopeFilter::conv_add/conv_del).
// 只存在与否, 不存任何密钥材料 -- XDP 拿它区分"已建立的会话"和"新会话尝试".
#define ACTIVE_CONV_MAX (1 << 17)

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, ACTIVE_CONV_MAX);
    __type(key, __u32);
    __type(value, __u8);
} active_conv SEC(".maps");


// 每源 IP 的"新会话尝试"计数, 固定 1 秒窗口
struct newsess_win {
    __u64 start_ns;
    __u32 count;
};

struct {
    __uint(type, BPF_MAP_TYPE_LRU_HASH);
    __uint(max_entries, 65536);
    __type(key, __u32);
    __type(value, struct newsess_win);
} newsess SEC(".maps");


// .rodata 变量按声明顺序排布, 用户态按固定偏移写入(见 envelope_filter.cpp):
// 偏移 0: target_port(u16) 偏移 4: nkeys(u32) 偏移 8: newsess_max(u32)
const volatile __u16 target_port = 0;

// 密钥数量(2 的幂), 由用户态在加载前写入 .rodata; 0 表示未配置 -> 退化成单把(下标 0)
const volatile __u32 nkeys = 0;

// 每个源 IP 每秒允许多少个"新会话尝试"; 0 = 不限速
const volatile __u32 newsess_max = 0;


// =============================================================================
//                             SipHash-2-4 (fixed 24 字节输入)
// =============================================================================

static __always_inline __u64
rotl64(__u64 x, int b) {
    return (x << b) | (x >> (64 - b));
}


// little-endian load 32-bit. 调用方保证 p[0..4) in-bounds.
static __always_inline __u32
load_le32(const __u8* p) {
    __u32 v = 0;
    v |= (__u32)p[0];
    v |= (__u32)p[1] << 8;
    v |= (__u32)p[2] << 16;
    v |= (__u32)p[3] << 24;
    return v;
}



// little-endian load 64-bit. 调用方保证 p[0..8) in-bounds.
static __always_inline __u64
load_le64(const __u8* p) {
    __u64 v = 0;
    v |= (__u64)p[0];
    v |= (__u64)p[1] << 8;
    v |= (__u64)p[2] << 16;
    v |= (__u64)p[3] << 24;
    v |= (__u64)p[4] << 32;
    v |= (__u64)p[5] << 40;
    v |= (__u64)p[6] << 48;
    v |= (__u64)p[7] << 56;
    return v;
}


static __always_inline int
key_is_zero(const __u8* k) {
    return (load_le64(k) | load_le64(k + 8)) == 0;
}


#define SIPROUND \
    do { \
        v0 += v1; v1 = rotl64(v1, 13); v1 ^= v0; v0 = rotl64(v0, 32); \
        v2 += v3; v3 = rotl64(v3, 16); v3 ^= v2; \
        v0 += v3; v3 = rotl64(v3, 21); v3 ^= v0; \
        v2 += v1; v1 = rotl64(v1, 17); v1 ^= v2; v2 = rotl64(v2, 32); \
    } while (0)


// 对 frame 前 24 字节 (3 整 block, 无 tail) 算 SipHash-2-4.
// 与 utils::siphash24(frame, 24, key) 位等价.
//
// 调用方必须保证 frame + 24 <= data_end (XDP 主程序里已经 check).
static __always_inline __u64
siphash24_xdp_fixed24(const __u8* frame, const __u8* key) {
    __u64 k0 = load_le64(key);
    __u64 k1 = load_le64(key + 8);

    __u64 v0 = k0 ^ 0x736f6d6570736575ULL;
    __u64 v1 = k1 ^ 0x646f72616e646f6dULL;
    __u64 v2 = k0 ^ 0x6c7967656e657261ULL;
    __u64 v3 = k1 ^ 0x7465646279746573ULL;

    __u64 m;

    // Block 0: frame[0..8)
    m = load_le64(frame + 0);
    v3 ^= m; SIPROUND; SIPROUND; v0 ^= m;

    // Block 1: frame[8..16)
    m = load_le64(frame + 8);
    v3 ^= m; SIPROUND; SIPROUND; v0 ^= m;

    // Block 2: frame[16..24)
    m = load_le64(frame + 16);
    v3 ^= m; SIPROUND; SIPROUND; v0 ^= m;

    // Final block: b = len << 56 (无 tail bytes 因 24 是 8 的倍数)
    __u64 b = (__u64)ENVELOPE_MAC_HASH_LEN << 56;
    v3 ^= b; SIPROUND; SIPROUND; v0 ^= b;

    // d=4 finalization
    v2 ^= 0xFF;
    SIPROUND; SIPROUND; SIPROUND; SIPROUND;

    return v0 ^ v1 ^ v2 ^ v3;
}


// =============================================================================
// 新会话尝试限速
// =============================================================================

// 固定 1 秒窗口计数, 不是令牌桶 -- 令牌桶要按经过时间按比例补充, 那就得做除法,
// 而 eBPF 里除法慢且 verifier 挑剔. 固定窗口的代价是跨边界那一瞬最多放行 2 倍,
// 对"防 4.3KB 分配 churn"这个目的完全无所谓.
//
// 注: 多个 RX 队列可能并发读改同一个计数, 这里不加锁 -- 限速本就允许近似,
// 少算几个远比在 XDP 热路径上背一把 spin lock 划算.
static __always_inline int
newsess_allow(__u32 saddr) {
    __u64 now = bpf_ktime_get_ns();

    struct newsess_win* w = bpf_map_lookup_elem(&newsess, &saddr);
    if (!w) {
        struct newsess_win init = { .start_ns = now, .count = 1 };
        bpf_map_update_elem(&newsess, &saddr, &init, BPF_ANY);
        return 1;
    }

    if (now - w->start_ns >= 1000000000ULL) {
        w->start_ns = now;
        w->count = 1;
        return 1;
    }

    if (w->count >= newsess_max) {
        return 0;
    }

    w->count++;
    return 1;
}


// =============================================================================
//                             XDP entry
// =============================================================================

SEC("xdp")
int
filter_envelope(struct xdp_md* ctx) {
    void* data     = (void*)(long)ctx->data;
    void* data_end = (void*)(long)ctx->data_end;

    // --- 1. 以太网头 ---
    struct ethhdr* eth = data;
    if ((void*)(eth + 1) > data_end) {
        return XDP_PASS;
    }

    if (eth->h_proto != bpf_htons(ETH_P_IP)) {
        return XDP_PASS;
    }

    // --- 2. IPv4 头 ---
    struct iphdr* ip = (struct iphdr*)(eth + 1);
    if ((void*)(ip + 1) > data_end) {
        return XDP_PASS;
    }

    if (ip->protocol != IPPROTO_UDP) {
        return XDP_PASS;
    }

    __u32 ip_hdr_len = ip->ihl * 4;
    if (ip_hdr_len < sizeof(*ip)) {
        return XDP_DROP;
    }

    if ((void*)ip + ip_hdr_len > data_end) {
        return XDP_DROP;
    }

    // --- 3. UDP 头 ---
    struct udphdr* udp = (struct udphdr*)((__u8*)ip + ip_hdr_len);
    if ((void*)(udp + 1) > data_end) {
        return XDP_DROP;
    }

    // 端口过滤
    if (target_port != 0 && udp->dest != bpf_htons(target_port)) {
        return XDP_PASS;
    }

    // --- 4. 长度 + bounds check ---
    // 完整 UDP payload (含 MAC) 至少 = MAC(8) + KCP header(24) = 32 字节.
    // 这里用 fixed-size constant offset bounds check, 让 verifier 精确 track
    // payload[0..MAC+HASH_LEN) 的可读范围.
    __u8* payload = (__u8*)(udp + 1);
    if (payload + ENVELOPE_MAC_LEN + ENVELOPE_MAC_HASH_LEN > (__u8*)data_end) {
        return XDP_DROP;
    }
    
    // 现在 payload[0..32) 全部 in-bounds, verifier 知道
    // - payload[0..8) = MAC
    // - payload[8..32) = KCP frame 前 24 字节 (= ENVELOPE_MAC_HASH_LEN)
    __u64 tag_wire = load_le64(payload);

    // conv 是 KCP 头的前 4 字节(小端), 紧跟在 8 字节 MAC 之后
    // 用它选密钥槽位: 攻击者手里只有自己 conv 对应的那一把, 伪造不了其他同余类
    __u32 conv = load_le32(payload + ENVELOPE_MAC_LEN);

    // nkeys 是 2 的幂(用户态已断言), 用 & 代替 % -- eBPF 里除法慢且 verifier 挑剔
    __u32 n = nkeys;
    if (n == 0) {
        return XDP_DROP;
    }

    __u32 key_idx = conv % n;
    if (key_idx >= ENVELOPE_KEY_SLOTS) {
        return XDP_DROP;
    }

    __u8* key = bpf_map_lookup_elem(&envelope_key, &key_idx);
    if (!key || key_is_zero(key)) {
        return XDP_DROP;
    }

    __u64 tag_calc = siphash24_xdp_fixed24(payload + ENVELOPE_MAC_LEN, key);
    if (tag_wire != tag_calc) {
        return XDP_DROP;
    }

    // MAC 过了, 但这还不代表它属于一个已建立的会话.
    // conv 不认识的包会让用户态新建一个 Session -- ikcp_create 一次分配约 4.3KB,
    // 建完发现是垃圾包再析构, 而且未注册会话收到非握手包还会回一个 RST(可被用作反射).
    // 拿到合法槽位密钥的攻击者在自己的同余类里乱撒 conv, 就能用 32 字节的包换服务端
    // 4.3KB 的分配 churn -- 这条路信封拦不住(信封校验在 Session 建好之后才发生).
    //
    // 只对"新会话尝试"限速, 已建立的会话一律放行: 否则 NAT 后面几十个玩家共用一个
    // 出口 IP, 会被当成一个来源一起误伤.
    if (newsess_max != 0 && bpf_map_lookup_elem(&active_conv, &conv) == NULL && !newsess_allow(ip->saddr)) {
        return XDP_DROP;
    }

    return XDP_PASS;
}


char _license[] SEC("license") = "GPL";
