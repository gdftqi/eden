#include "tcp/server.hpp"


class EchoService: public typhon::tcp::Server::IEvent {
public:
    virtual int 
    on_init(typhon::tcp::Server* server) noexcept {
        xINFO("{} running on {}", server->host(), ::pthread_self());
        return 0;
    }

    virtual void
    on_stopped(typhon::tcp::Server* server) noexcept {
        xINFO("{} stopped on {}", server->host(), ::pthread_self());
    }

    virtual int
    on_connected(typhon::tcp::Session::Ptr sess) noexcept {
        xINFO("{} connected on {}", sess->remote_addr(), ::pthread_self());
        return 0;
    }

    virtual void
    on_disconnected(typhon::tcp::Session::Ptr sess) noexcept {
        xINFO("{} disconnected on {}", sess->remote_addr(), ::pthread_self());
    }
};

int
main(int argc, char** argv) {
    EchoService service;
    typhon::tcp::Server server("0.0.0.0:6688", &service);
    server.run();
    ::exit(EXIT_SUCCESS);
}