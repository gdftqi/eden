#ifndef __TYPHON_HPP__
#define __TYPHON_HPP__


#include "bpf_router.hpp"
#include "kcp_server.hpp"
#include "utils/log.hpp"


namespace typhon {


/**
 * @brief Typhon 服务
 */
class TyService {
    TyService(const TyService&) = delete;
    TyService& operator=(const TyService&) = delete;
    TyService(TyService&&) = delete;
    TyService& operator=(TyService&&) = delete;


public:
    /**
     * @brief 构造函数
     * @param ev KcpServer 事件接口
     * @param host 监听端口
     * @param bpf_obj_path eBPF 路径
     */
    explicit TyService(KcpServer::IEvent* ev, const char* host, const char* bpf_obj_path) noexcept
        : serv_ev_(ev)
        , host_(host), bpf_obj_path_(bpf_obj_path)
    {}


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
    KcpServer::IEvent*          serv_ev_       { nullptr };        // 服务事件
    std::atomic<State>          state_         { State::Stopped }; // 状态
    std::string                 host_;                             // 服务地址
    std::string                 bpf_obj_path_;                     // eBPF 路径
    BpfRouter                   router_;                           // eBPF 路由
    std::vector<KcpServer::Ptr> servers_;                          // kcp servers
    std::vector<std::thread>    threads_;                          // 线程池
};


} // namespace typhon;


#endif // __TYPHON_HPP__