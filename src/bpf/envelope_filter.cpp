#include "bpf/envelope_filter.hpp"

#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include <linux/if_link.h>      // XDP_FLAGS_*
#include <net/if.h>             // if_nametoindex
#include <cstring>
#include <errno.h>
#include "utils/log.hpp"


namespace {


// 在 BPF object 的所有 maps 里找名字包含 ".rodata" 的那个.
// libbpf 给 .rodata 段的 map 命名形如 "<obj_name>.rodata".
::bpf_map*
find_rodata_map(::bpf_object* obj) noexcept {
    ::bpf_map* m = nullptr;
    bpf_object__for_each_map(m, obj) {
        const char* name = ::bpf_map__name(m);
        if (name && std::strstr(name, ".rodata")) {
            return m;
        }
    }
    return nullptr;
}


} // anonymous namespace


typhon::bpf::EnvelopeFilter::~EnvelopeFilter() noexcept {
    if (if_index_ >= 0) {
        detach();
    }
    if (obj_) {
        ::bpf_object__close(obj_);
        obj_ = nullptr;
    }
}


int
typhon::bpf::EnvelopeFilter::init(const char* obj_path, uint16_t target_port, const uint8_t key[KEY_LEN]) noexcept {
    if (!obj_path || !key) {
        return -EINVAL;
    }

    obj_ = ::bpf_object__open_file(obj_path, nullptr);
    if (!obj_) {
        return -1;
    }

    // 1. 写 target_port 到 .rodata
    //    envelope.bpf.c 里只有一个 const volatile (target_port, __u16),
    //    所以 .rodata map value 大小就是它 (可能加 padding).
    //    用整个 map size 写, 避免 padding 假设.
    ::bpf_map* rodata = find_rodata_map(obj_);
    if (!rodata) {
        ::bpf_object__close(obj_);
        obj_ = nullptr;
        return -2;
    }

    {
        size_t rodata_size = ::bpf_map__value_size(rodata);
        // 用 stack buffer 装整个 .rodata 段;
        // envelope.bpf.c 的 .rodata 很小 (几字节), 256 字节绰绰有余.
        if (rodata_size > 256) {
            ::bpf_object__close(obj_);
            obj_ = nullptr;
            return -3;
        }
        uint8_t buf[256] = {};
        // target_port 是 .rodata 段第一个 (也是唯一一个) 变量, 写在 offset 0.
        std::memcpy(buf, &target_port, sizeof(target_port));
        if (::bpf_map__set_initial_value(rodata, buf, rodata_size)) {
            ::bpf_object__close(obj_);
            obj_ = nullptr;
            return -4;
        }
    }

    // 2. 把整个 object load 到内核 (verifier 在这一步跑)
    if (::bpf_object__load(obj_)) {
        ::bpf_object__close(obj_);
        obj_ = nullptr;
        return -5;
    }

    // 3. 取 program / map handle
    auto* prog    = ::bpf_object__find_program_by_name(obj_, "filter_envelope");
    auto* key_map = ::bpf_object__find_map_by_name(obj_, "envelope_key");
    if (!prog || !key_map) {
        ::bpf_object__close(obj_);
        obj_ = nullptr;
        return -6;
    }

    prog_fd_    = ::bpf_program__fd(prog);
    key_map_fd_ = ::bpf_map__fd(key_map);

    // 4. 写 key 到 slot 0 (current). slot 1 (previous) 不动, 初始为 0.
    uint32_t idx = 0;
    if (::bpf_map_update_elem(key_map_fd_, &idx, key, BPF_ANY)) {
        int err = -errno;
        ::bpf_object__close(obj_);
        obj_ = nullptr;
        prog_fd_ = -1;
        key_map_fd_ = -1;
        return err;
    }

    return 0;
}


int
typhon::bpf::EnvelopeFilter::attach(const char* ifname) noexcept {
    if (!ifname || prog_fd_ < 0) {
        return -EINVAL;
    }
    if (if_index_ >= 0) {
        return -EALREADY;
    }

    int idx = (int)::if_nametoindex(ifname);
    if (idx == 0) {
        return -errno;
    }

    // 先试 native mode (网卡驱动直接处理 XDP, 性能最优).
    int err = ::bpf_xdp_attach(idx, prog_fd_, XDP_FLAGS_DRV_MODE, nullptr);
    xdp_flags_ = XDP_FLAGS_DRV_MODE;
    if (err) {
        // fallback generic / SKB mode (内核协议栈处理后再丢, 兼容性最好).
        err = ::bpf_xdp_attach(idx, prog_fd_, XDP_FLAGS_SKB_MODE, nullptr);
        if (err) {
            return err;
        }
        xdp_flags_ = XDP_FLAGS_SKB_MODE;
    }

    if (xdp_flags_ == XDP_FLAGS_DRV_MODE) {
        xINFO("----- XDP DRV MODE(native) -----");
    } else {
        xINFO("----- XDP SKB MODE(generic) -----");
    }

    if_index_ = idx;
    return 0;
}


int
typhon::bpf::EnvelopeFilter::detach() noexcept {
    if (if_index_ < 0) {
        return 0;
    }

    int err = ::bpf_xdp_detach(if_index_, xdp_flags_, nullptr);
    if (err) {
        return err;
    }

    if_index_  = -1;
    xdp_flags_ = 0;
    return 0;
}


int
typhon::bpf::EnvelopeFilter::rotate_key(const uint8_t new_key[KEY_LEN]) noexcept {
    if (!new_key || key_map_fd_ < 0) {
        return -EINVAL;
    }

    // 1. 读 slot 0 (current)
    uint8_t current_key[KEY_LEN];
    uint32_t idx = 0;
    if (::bpf_map_lookup_elem(key_map_fd_, &idx, current_key)) {
        return -errno;
    }

    // 2. 写到 slot 1 (previous)
    idx = 1;
    if (::bpf_map_update_elem(key_map_fd_, &idx, current_key, BPF_ANY)) {
        return -errno;
    }

    // 3. 写 new_key 到 slot 0 (current)
    idx = 0;
    if (::bpf_map_update_elem(key_map_fd_, &idx, new_key, BPF_ANY)) {
        return -errno;
    }

    return 0;
}