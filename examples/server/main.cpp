#include <csignal>
#include <iostream>

#include "typhon.hpp"


static typhon::TyService* g_server = nullptr;


static void
on_signal(int) {
    if (g_server) {
        g_server->stop();
    }
}


class EchoService: public typhon::KcpServer::IEvent {
public:
    virtual int 
    on_init(typhon::KcpServer* server) noexcept {
        xINFO("{} running on {}", server->host(), ::pthread_self());
        return 0;
    }


    virtual void
    on_stopped(typhon::KcpServer* server) noexcept {
        xINFO("{} stopped on {}", server->host(), ::pthread_self());
    }


    virtual int
    on_connected(typhon::Kcp::Ptr kcp) noexcept {
        xINFO("{} connected on {}", kcp->remote_addr(), ::pthread_self());
        return 0;
    }


    virtual void
    on_disconnected(typhon::Kcp::Ptr kcp) noexcept {
        xINFO("{} disconnected on {}", kcp->remote_addr(), ::pthread_self());
    }


    virtual int
    on_data(typhon::Kcp::Ptr kcp, const typhon::Package* pkg) noexcept {
        return kcp->send_pk(
            pkg->pk_id, 
            kcp->next_snd_idem(), 
            pkg->pk_dst_id, 
            pkg->pk_data, 
            pkg->pk_len - sizeof(typhon::Package)
        ) < 0 ? -1 : 0;
    }
};


int
main(int argc, char** argv) {
    const char* bpf_path = (argc > 1) ? argv[1] : "";

    EchoService echo;
    typhon::TyService server(&echo, "0.0.0.0:5555", bpf_path);
    g_server = &server;

    ::signal(SIGINT, on_signal);
    ::signal(SIGTERM, on_signal);

    xINFO("kcp echo listening on 0.0.0.0:5555");
    server.run();
    exit(0);
}
