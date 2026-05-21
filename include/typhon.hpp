#ifndef __TYPHON_HPP__
#define __TYPHON_HPP__


#include "bpf/router.hpp"
#include "kcp/server.hpp"
#include "tcp/server.hpp"


namespace typhon {


/**
 * @brief Typhon 服务
 */
class Server {
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    Server(Server&&) = delete;
    Server& operator=(Server&&) = delete;


public:
    /**
     * @brief 构造函数
     * @param ev KcpServer 事件接口
     * @param host 监听端口
     * @param bpf_obj_path eBPF 路径
     */
    explicit Server(kcp::Server::IEvent* ev, const char* host, const char* bpf_obj_path = "") noexcept
        : serv_ev_(ev)
        , host_(host)
        , bpf_obj_path_(bpf_obj_path ? bpf_obj_path : "") {
        ASSERT(host_.find(':') != std::string::npos, "invalid host: {}", host_);
    }


    /**
     * @brief 启动服务
     */
    void
    run() noexcept;


    /**
     * @brief 停止服务
     */
    void
    stop() noexcept;


private:
    kcp::Server::IEvent*          serv_ev_       { nullptr };              // 服务事件
    std::atomic<core::State>      state_         { core::State::Stopped }; // 状态
    std::string                   host_;                                   // 服务地址
    std::string                   bpf_obj_path_;                           // eBPF 路径
    bpf::Router                   router_;                                 // eBPF 路由
    std::vector<kcp::Server::Ptr> servers_;                                // kcp servers
    std::vector<std::thread>      threads_;                                // 线程池
};


} // namespace typhon;


#endif // __TYPHON_HPP__