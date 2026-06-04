#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>


#define MAX_WORKERS 64   // 上限, map 永远开这么大; 运行时由 num_workers 决定实际用几个


static inline __u32 
read_conv_le(__u32 conv) {
#if (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
    return conv;
#else
    __u8* p = (__u8*)&conv;
    return (__u32)p[0] | ((__u32)p[1] << 8) | ((__u32)p[2] << 16) | ((__u32)p[3] << 24);
#endif
}


struct {
    __uint(type, BPF_MAP_TYPE_REUSEPORT_SOCKARRAY);
    __uint(max_entries, MAX_WORKERS);
    __type(key, __u32);
    __type(value, __u64);
} sock_map SEC(".maps");


// userspace 在 bpf_object__load() 前改写这个值(写到 .rodata 段).
// 默认 1, 万一 userspace 忘改也只是全打到 worker 0, 不会崩.
const volatile __u32 num_workers = 1;


// kernel 传给 sk_reuseport BPF 程序的 ctx->data 指向 UDP **头** (不是 payload).
// UDP payload 布局 (与 envelope.bpf.c / cryptor.cpp / buffer.hpp 严格对齐):
//   [UDP 头 8B][envelope MAC 8B][KCP frame: conv(4B) + cmd(1B) + ...]
//                                ↑ KCP conv 起始
// 所以 conv 在 ctx->data 偏移 8 + 8 = 16 字节处.
#define UDP_HDR_LEN         8
#define ENVELOPE_MAC_LEN    8     // 与 core::ENVELOPE_MAC_LEN 同


SEC("sk_reuseport") int
select_by_conv(struct sk_reuseport_md *ctx) {
    void* data = ctx->data;
    void* data_end = ctx->data_end;

    if (data + UDP_HDR_LEN + ENVELOPE_MAC_LEN + 4 > data_end) {
        return SK_DROP;
    }

    __u32 conv = read_conv_le(*(__u32*)((char*)data + UDP_HDR_LEN + ENVELOPE_MAC_LEN));
    if (conv == 0) {
        return SK_DROP;
    }

    __u32 idx = conv % num_workers;
    return bpf_sk_select_reuseport(ctx, &sock_map, &idx, 0) == 0 ? SK_PASS : SK_DROP;
}

char _license[] SEC("license") = "GPL";