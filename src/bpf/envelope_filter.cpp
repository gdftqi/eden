#include "bpf/envelope_filter.hpp"

#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include <linux/if_link.h>      // XDP_FLAGS_*
#include <net/if.h>             // if_nametoindex
#include <cstring>
#include <errno.h>
#include "utils/log.hpp"
#include "utils/string_ex.hpp"
#include "core/error.hpp"


// 在 BPF object 的所有 maps 里找名字包含 ".rodata" 的那个.
// libbpf 给 .rodata 段的 map 命名形如 "<obj_name>.rodata".
static ::bpf_map*
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
typhon::bpf::EnvelopeFilter::init(const char* obj_path, uint16_t udp_port, const uint8_t key[KEY_LEN]) noexcept {
    if (!obj_path || !key) {
        return -EINVAL;
    }

    ASSERT(udp_port > 0, "无效的 UDP 端口");

    obj_ = ::bpf_object__open_file(obj_path, nullptr);
    if (!obj_) {
        xERROR("打开 {} 文件失败", std::string(obj_path));
        return xERR_BPF_OPEN;
    }

    ::bpf_map* rodata = find_rodata_map(obj_);
    if (!rodata) {
        ::bpf_object__close(obj_);
        obj_ = nullptr;
        xERROR(".rodata 已存在");
        return xERR_BPF_RODATA;
    }

    {
        size_t rodata_size = ::bpf_map__value_size(rodata);
        if (rodata_size > 256) {
            ::bpf_object__close(obj_);
            obj_ = nullptr;
            xERROR(".rodata map 大小 256 字节");
            return xERR_BPF_RODATA;
        }

        uint8_t buf[256] = {};
        // udp_port 是 .rodata 段第一个 (也是唯一一个) 变量, 写在 offset 0.
        std::memcpy(buf, &udp_port, sizeof(udp_port));
        if (::bpf_map__set_initial_value(rodata, buf, rodata_size)) {
            ::bpf_object__close(obj_);
            obj_ = nullptr;
            xERROR("写入 {} 端口到 .rodata 段中失败", udp_port);
            return xERR_BPF_RODATA;
        }
    }

    if (::bpf_object__load(obj_)) {
        ::bpf_object__close(obj_);
        obj_ = nullptr;
        xERROR("{} 加载到内核失败", std::string(obj_path));
        return xERR_BPF_LOAD;
    }

    // filter_envelope: envelope.bpf.c:136 函数签名
    auto* prog    = ::bpf_object__find_program_by_name(obj_, "filter_envelope");

    // envelope_key: envelope.bpf.c:49 map 的变量名
    auto* key_map = ::bpf_object__find_map_by_name(obj_, "envelope_key");
    if (!prog || !key_map) {
        ::bpf_object__close(obj_);
        obj_ = nullptr;
        xERROR("查询 filter_envelope 或 envelope_key 失败");
        return xERR_BPF_FIND;
    }

    prog_fd_    = ::bpf_program__fd(prog);
    key_map_fd_ = ::bpf_map__fd(key_map);

    uint32_t idx = 0;
    if (::bpf_map_update_elem(key_map_fd_, &idx, key, BPF_ANY)) {
        int err = -errno;
        ::bpf_object__close(obj_);
        obj_ = nullptr;
        prog_fd_ = -1;
        key_map_fd_ = -1;
        xERROR("更新 Key {} 值失败", utils::bytes_to_hex(key, KEY_LEN));
        return err;
    }

    return xOK;
}


int
typhon::bpf::EnvelopeFilter::attach(const char* ifname) noexcept {
    ASSERT(ifname != nullptr && prog_fd_ >= 0, "EnvelopeFilter 参数错误");

    if (if_index_ >= 0) {
        xERROR("已绑定 ifname");
        return -EALREADY;
    }

    int idx = (int)::if_nametoindex(ifname);
    if (idx == 0) {
        return -errno;
    }

    // 先试 native mode (网卡驱动直接处理 XDP, 性能最优)
    int err = ::bpf_xdp_attach(idx, prog_fd_, XDP_FLAGS_DRV_MODE, nullptr);
    xdp_flags_ = XDP_FLAGS_DRV_MODE;
    if (err) {
        // fallback generic / SKB mode (内核协议栈处理后再丢, 兼容性最好)
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
    return xOK;
}


int
typhon::bpf::EnvelopeFilter::detach() noexcept {
    if (if_index_ < 0) {
        return xOK;
    }

    int err = ::bpf_xdp_detach(if_index_, xdp_flags_, nullptr);
    if (err) {
        return err;
    }

    if_index_  = -1;
    xdp_flags_ = 0;
    return xOK;
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

    return xOK;
}