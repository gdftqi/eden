#include "bpf/envelope_filter.hpp"

#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include <linux/if_link.h>
#include <net/if.h>
#include <cstring>
#include <errno.h>
#include "utils/log.hpp"
#include "utils/string_ex.hpp"
#include "core/error.hpp"


// 在 BPF object 的所有 maps 里找常量段, libbpf 给它的命名形如 "<obj_name>.rodata".
// 必须是"以 .rodata 结尾"
static ::bpf_map*
find_rodata_map(::bpf_object* obj) noexcept {
    static constexpr char   SUFFIX[]   = ".rodata";
    static constexpr size_t SUFFIX_LEN = sizeof(SUFFIX) - 1;

    ::bpf_map* m = nullptr;
    bpf_object__for_each_map(m, obj) {
        const char* name = ::bpf_map__name(m);
        if (name == nullptr) {
            continue;
        }

        size_t len = std::strlen(name);
        if (len >= SUFFIX_LEN && std::strcmp(name + len - SUFFIX_LEN, SUFFIX) == 0) {
            return m;
        }
    }

    return nullptr;
}


adam::bpf::EnvelopeFilter::~EnvelopeFilter() noexcept {
    if (if_index_ >= 0) {
        detach();
    }

    if (obj_) {
        ::bpf_object__close(obj_);
        obj_ = nullptr;
    }
}


int
adam::bpf::EnvelopeFilter::init(const char* obj_path, uint16_t udp_port, const SipHashKey* keys, int keys_count) noexcept {
    if (!obj_path || keys == nullptr || keys_count == 0) {
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
        xERROR("找不到 .rodata map");
        return xERR_BPF_RODATA;
    }

    {
        // .rodata 段变量按声明顺序排布(见 envelope.bpf.c):
        //   offset 0: const volatile __u16 target_port
        //   offset 4: const volatile __u32 nkeys   (u32 对齐到 4)
        constexpr size_t RODATA_MIN = 8;   // 上面两个变量占满的字节数

        size_t rodata_size = ::bpf_map__value_size(rodata);

        // 下界同样要查: 选错 map(或 .bpf.c 里删了变量)的表现就是段变小,
        // 此时下面按固定偏移写就成了越界/无效写, 必须在这里拦住。
        if (rodata_size < RODATA_MIN || rodata_size > 256) {
            ::bpf_object__close(obj_);
            obj_ = nullptr;
            xERROR(".rodata map 大小异常: {} 字节(应在 [{}, 256] 内)", rodata_size, RODATA_MIN);
            return xERR_BPF_RODATA;
        }

        uint8_t buf[256] = {};
        uint32_t n = (uint32_t)keys_count;
        std::memcpy(buf + 0, &udp_port, sizeof(udp_port));
        std::memcpy(buf + 4, &n,        sizeof(n));
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
    auto* prog = ::bpf_object__find_program_by_name(obj_, "filter_envelope");

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

    for (int i = 0; i < keys_count; ++i) {
        uint32_t idx = (uint32_t)i;
        if (::bpf_map_update_elem(key_map_fd_, &idx, keys[i], BPF_ANY)) {
            int err = -errno;
            ::bpf_object__close(obj_);
            obj_ = nullptr;
            prog_fd_ = -1;
            key_map_fd_ = -1;
            xERROR("写入第 {} 把 envelope key 失败", i);
            return err;
        }
    }

    return xOK;
}


int
adam::bpf::EnvelopeFilter::attach(const char* ifname) noexcept {
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
adam::bpf::EnvelopeFilter::detach() noexcept {
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