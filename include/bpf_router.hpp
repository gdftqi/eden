#ifndef __TYPHON_BPF_ROUTER_HPP__
#define __TYPHON_BPF_ROUTER_HPP__


#include <cstdint>

struct bpf_object;


namespace typhon {


class BpfRouter {
    BpfRouter(const BpfRouter&) = delete;
    BpfRouter& operator=(const BpfRouter&) = delete;
    BpfRouter(BpfRouter&&) = delete;
    BpfRouter& operator=(BpfRouter&&) = delete;


public:
    BpfRouter() noexcept
    {}


    ~BpfRouter() noexcept;


    int
    init(const char* obj_path, uint32_t num_workers) noexcept;


    int
    register_socket(uint32_t idx, int sockfd) noexcept;


    int
    attach(int sockfd) noexcept;


private:
    ::bpf_object* obj_      { nullptr };
    int           map_fd_   { -1 };
    int           prog_fd_  { -1 };
};


} // namespace typhon


#endif // __TYPHON_BPF_ROUTER_HPP__