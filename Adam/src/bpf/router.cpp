#include "bpf/router.hpp"
#include "core/error.hpp"

#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include <sys/socket.h>
#include "utils/log.hpp"


adam::bpf::Router::~Router() noexcept {
    if (obj_) {
        ::bpf_object__close(obj_);
        obj_ = nullptr;
    }
}


int
adam::bpf::Router::init(const char* obj_path, uint32_t num_workers) noexcept {
    ASSERT(obj_path && num_workers > 0, "无效的入参");

    obj_ = ::bpf_object__open_file(obj_path, nullptr);
    if (!obj_) {
        return xERR_BPF_OPEN;
    }

    // 在 load 之前把 rodata 里的 num_workers 改写.
    // BPF 里声明的 `const volatile __u32 num_workers` 会被放进 .rodata map
    ::bpf_map* rodata = nullptr;
    ::bpf_map* m;
    bpf_object__for_each_map(m, obj_) {
        const char* name = ::bpf_map__name(m);
        if (name && std::strstr(name, ".rodata")) {
            rodata = m;
            break;
        }
    }

    if (!rodata) {
        ::bpf_object__close(obj_);
        obj_ = nullptr;
        return xERR_BPF_RODATA;
    }
    
    if (::bpf_map__set_initial_value(rodata, &num_workers, sizeof(num_workers))) {
        ::bpf_object__close(obj_);
        obj_ = nullptr;
        return xERR_BPF_RODATA;
    }

    if (::bpf_object__load(obj_)) {
        ::bpf_object__close(obj_);
        obj_ = nullptr;
        return xERR_BPF_LOAD;
    }

    auto* sock_map = ::bpf_object__find_map_by_name(obj_, "sock_map");
    auto* prog     = ::bpf_object__find_program_by_name(obj_, "select_by_conv");
    if (!sock_map || !prog) {
        ::bpf_object__close(obj_);
        obj_ = nullptr;
        return xERR_BPF_FIND;
    }

    map_fd_  = ::bpf_map__fd(sock_map);
    prog_fd_ = ::bpf_program__fd(prog);

    return xOK;
}


int
adam::bpf::Router::register_socket(uint32_t idx, int ufd) noexcept {
    ASSERT(map_fd_ >= 0, "Router 还未初始化");

    uint64_t fd64 = (uint64_t)ufd;
    return ::bpf_map_update_elem(map_fd_, &idx, &fd64, BPF_ANY);
}


int
adam::bpf::Router::attach(int ufd) noexcept {
    ASSERT(prog_fd_ >= 0 && ufd >= 0, "Router 参数无效");

    if (::setsockopt(ufd, SOL_SOCKET, SO_ATTACH_REUSEPORT_EBPF, &prog_fd_, sizeof(prog_fd_)) < 0) {
        return -errno;
    }

    return xOK;
}