#ifndef __SERVER_HPP__
#define __SERVER_HPP__


#include <thread>
#include <vector>
#include "kcp_server.hpp"
#include "bpf_router.hpp"


namespace typhon {


class Server {
    typedef std::unique_ptr<KcpServer> KcpServerPtr;


    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    Server(Server&&) = delete;
    Server& operator=(Server&&) = delete;


public:
    explicit Server(const char* host, const char* bpf_obj_path) noexcept
        : host_(host), bpf_obj_path_(bpf_obj_path)
    {}


    ~Server() noexcept
    {}


    void
    run() noexcept;


    void
    stop() noexcept;


private:
    std::atomic<State>           state_ { State::Stopped };
    std::string                  host_;
    std::string                  bpf_obj_path_;
    BpfRouter                    router_;
    std::vector<KcpServerPtr>    servers_;
    std::vector<std::thread>     threads_;
};


} // namespace typhon;


#endif // __SERVER_HPP__
