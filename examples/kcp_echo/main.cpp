#include <csignal>
#include <iostream>

#include "typhon.hpp"


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
    on_data(typhon::kcp::Session::Ptr sess, typhon::core::PK<typhon::core::Host> &pk) noexcept {
        return sess->send(pk) < 0 ? -1 : 0;
    }
};


int
main(int argc, char** argv) {
    // 位置参数:
    //   argv[1] : ifname              (默认 "lo", 开发用; 生产传真实网卡)
    //   argv[2] : kcp_bpf_path        (默认 "build/kcp.bpf.o")
    //   argv[3] : envelope_bpf_path   (默认 "build/envelope.bpf.o")
    const char* config            = "config.yml";
    const char* ifname            = (argc > 1) ? argv[1] : "lo";
    const char* kcp_bpf_path      = (argc > 2) ? argv[2] : "build/kcp.bpf.o";
    const char* envelope_bpf_path = (argc > 3) ? argv[3] : "build/envelope.bpf.o";

    if (!typhon::utils::lock_pid("server.pid")) {
        xERROR("程序已启动");
        ::exit(EXIT_FAILURE);
    }

    typhon::Conf::instance()->load(config);

    EchoService echo;
    typhon::Server server(&echo);
    g_server = &server;

    ::signal(SIGINT, on_signal);
    ::signal(SIGTERM, on_signal);

    xINFO("kcp echo listening on 0.0.0.0:5555 (XDP on {}, kcp bpf = {}, envelope bpf = {})",
          ifname, kcp_bpf_path, envelope_bpf_path);
    server.run();
    ::exit(EXIT_SUCCESS);
}
