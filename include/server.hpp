#ifndef __SERVER_HPP__
#define __SERVER_HPP__


#include <thread>
#include "kcp_server.hpp"


namespace typhon {


class Server {
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    Server(Server&&) = delete;
    Server& operator=(Server&&) = delete;


public:
    explicit Server(const char* host) noexcept
        : host_(host)
    {}


    ~Server() noexcept
    {}


    void
    run() noexcept;


    void
    stop() noexcept;


private:
    std::atomic<State> state_ { State::Stopped };
    std::string host_;
    std::vector<KcpServer*> servers_;
    std::vector<std::thread> threads_;
};


} // namespace typhon;


#endif // __SERVER_HPP__