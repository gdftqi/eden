#ifndef __TYPHON_HPP__
#define __TYPHON_HPP__


#include <thread>
#include <vector>
#include "kcp_server.hpp"
#include "bpf_router.hpp"


namespace typhon {


class TyService {
    typedef std::unique_ptr<KcpServer> KcpServerPtr;


    TyService(const TyService&) = delete;
    TyService& operator=(const TyService&) = delete;
    TyService(TyService&&) = delete;
    TyService& operator=(TyService&&) = delete;


public:
    explicit TyService(KcpServer::IEvent* ev, const char* host, const char* bpf_obj_path) noexcept
        : ev_(ev)
        , host_(host), bpf_obj_path_(bpf_obj_path)
    {}


    ~TyService() noexcept
    {}


    void
    run() noexcept;


    void
    stop() noexcept;


private:
    KcpServer::IEvent*          ev_             { nullptr };
    std::atomic<State>          state_          { State::Stopped };
    std::string                 host_;
    std::string                 bpf_obj_path_;
    BpfRouter                   router_;
    std::vector<KcpServerPtr>   servers_;
    std::vector<std::thread>    threads_;
};


} // namespace typhon;


#endif // __TYPHON_HPP__
