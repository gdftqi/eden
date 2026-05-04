#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>


#define NUM_SOCKETS 8


struct {
    __uint(type, BPF_MAP_TYPE_REUSEPORT_SOCKARRAY);
    __uint(max_entries, NUM_SOCKETS);
    __type(key, __u32);
    __type(value, __u64);
} sock_map SEC(".maps");


SEC("sk_reuseport") int
select_by_conv(struct sk_reuseport_md *ctx) {
    void* data = ctx->data;
    void* data_end = ctx->data_end;

    if (data + 4 > data_end) {
        return SK_DROP;
    }

    __u32 conv = *(__u32*)data;
    __u32 idx = conv % NUM_SOCKETS;

    if (bpf_sk_select_reuseport(ctx, &sock_map, &idx, 0) == 0) {
        return SK_PASS;
    }

    return SK_DROP;
}

char _license[] SEC("license") = "GPL";
    