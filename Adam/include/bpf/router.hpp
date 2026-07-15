#ifndef __ADAM_BPF_ROUTER_HPP__
#define __ADAM_BPF_ROUTER_HPP__


#include <cstdint>


struct bpf_object;


namespace adam::bpf {


/**
 * @brief kcp conv 路由线程绑定
 * 
 * 根据 conv 来分派 kcp server 线程
 */
class Router {
    Router(const Router&) = delete;
    Router& operator=(const Router&) = delete;
    Router(Router&&) = delete;
    Router& operator=(Router&&) = delete;


public:
    explicit
    Router() noexcept
    {}


    ~Router() noexcept;


    /**
     * @brief 初始化
     * 
     * @param obj_path    kcp.bpf.o 文件路径
     * @param num_workers 工作线程数
     * 
     * @return 成功返回 0, 否则返回 错误码
     */
    int
    init(const char* obj_path, uint32_t num_workers) noexcept;


    /**
     * @brief 注册 udp sockfd
     * 
     * @param idx num_workers 的编号
     * @param ufd udp sockfd
     * 
     * @return 成功返回 0, 否则返回 错误码
     */
    int
    register_socket(uint32_t idx, int ufd) noexcept;


    /**
     * @brief 为 udp sockfd 挂载 REUSEPORT 程序
     * 
     * @param ufd 需要挂载的 REUSEPORT 的 udp sockfd
     * 
     * @return 成功返回 0, 否则返回 错误码
     */
    int
    attach(int ufd) noexcept;


private:
    ::bpf_object* obj_      { nullptr }; ///< eBPF 对象
    int           map_fd_   { -1 };      ///< sock_map 路由表
    int           prog_fd_  { -1 };      ///< eBPF 程序
};


} // namespace adam::bpf;


#endif // __ADAM_BPF_ROUTER_HPP__