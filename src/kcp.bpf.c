#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>


#define MAX_WORKERS 64   // 上限，map 永远开这么大；运行时由 num_workers 决定实际用几个


struct {
    __uint(type, BPF_MAP_TYPE_REUSEPORT_SOCKARRAY);
    __uint(max_entries, MAX_WORKERS);
    __type(key, __u32);
    __type(value, __u64);
} sock_map SEC(".maps");


// userspace 在 bpf_object__load() 前改写这个值（写到 .rodata 段）。
// 默认 1，万一 userspace 忘改也只是全打到 worker 0，不会崩。
const volatile __u32 num_workers = 1;


// kernel 传给 sk_reuseport BPF 程序的 ctx->data 指向 UDP **头**（不是 payload）。
// UDP 头固定 8 字节，KCP conv 紧随其后。所以我们要读 [data+8, data+12)。
#define UDP_HDR_LEN 8

SEC("sk_reuseport") int
select_by_conv(struct sk_reuseport_md *ctx) {
    void* data = ctx->data;
    void* data_end = ctx->data_end;

    if (data + UDP_HDR_LEN + 4 > data_end) {
        return SK_DROP;
    }

    __u32 conv = *(__u32*)((char*)data + UDP_HDR_LEN);
    __u32 idx = conv % num_workers;

    int rc = bpf_sk_select_reuseport(ctx, &sock_map, &idx, 0);

    if (rc == 0) {
        return SK_PASS;
    }
    return SK_DROP;
}

char _license[] SEC("license") = "GPL";