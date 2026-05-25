#include <csignal>
#include <iostream>

#include "typhon.hpp"
#include "utils/sys.hpp"


static typhon::Server* g_server = nullptr;


static void
on_signal(int) {
    if (g_server) {
        g_server->stop();
    }
}


class EchoService: public typhon::kcp::Server::IEvent {
public:
    virtual void 
    on_init(typhon::kcp::Server* server) noexcept {
        xINFO("{} running on {}", server->host(), ::pthread_self());
    }


    virtual void
    on_stopped(typhon::kcp::Server* server) noexcept {
        xINFO("{} stopped on {}", server->host(), ::pthread_self());
    }


    virtual int
    on_connected(typhon::kcp::Session::Ptr kcp) noexcept {
        xINFO("{} connected on {}", kcp->remote_addr(), ::pthread_self());
        return 0;
    }


    virtual void
    on_disconnected(typhon::kcp::Session::Ptr kcp) noexcept {
        xINFO("{} disconnected on {}", kcp->remote_addr(), ::pthread_self());
    }


    virtual int
    on_data(typhon::kcp::Session::Ptr kcp, const typhon::core::Package* pk, int len) noexcept {
        return kcp->send_pk(
            pk->pk_id, 
            kcp->next_snd_idem(), 
            pk->pk_dst_id, 
            pk->pk_payload, 
            len - sizeof(typhon::core::Package)
        ) < 0 ? -1 : 0;
    }
};


int
main(int argc, char** argv) {
    const char* bpf_path = (argc > 1) ? argv[1] : "";

    if (!typhon::utils::lock_pid("server.pid")) {
        xERROR("程序已启动");
        ::exit(EXIT_FAILURE);
    }

    EchoService echo;
    typhon::Server server(&echo, "0.0.0.0:5555", bpf_path);
    g_server = &server;

    ::signal(SIGINT, on_signal);
    ::signal(SIGTERM, on_signal);

    xINFO("kcp echo listening on 0.0.0.0:5555");
    server.run();
    ::exit(EXIT_SUCCESS);
}
