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


class EchoService: public typhon::kcp::IEvent {
public:
    virtual void 
    on_init(void* arg) noexcept {
        server_ = (typhon::Server*)arg;
        xINFO("{} is running...", typhon::Conf::instance()->host());
    }


    virtual void
    on_stopped(void*) noexcept {
        xINFO("{} stopped", typhon::Conf::instance()->host());
    }


    virtual int
    on_sess_connected(typhon::kcp::Session::Ptr kcp) noexcept {
        xINFO("{} connected on {}", kcp->remote_addr(), ::pthread_self());
        return 0;
    }


    virtual void
    on_sess_disconnected(typhon::kcp::Session::Ptr kcp) noexcept {
        xINFO("{} disconnected on {}", kcp->remote_addr(), ::pthread_self());
    }


    virtual void
    on_user_connected(typhon::kcp::Session::Ptr kcp) noexcept {
        xINFO("[{}:{}] authed on {}", kcp->user_id(), kcp->remote_addr(), ::pthread_self());
    }


    virtual void
    on_user_disconnected(typhon::kcp::Session::Ptr kcp) noexcept {
        xINFO("[{}:{}] unauth on {}", kcp->user_id(), kcp->remote_addr(), ::pthread_self());
    }


    virtual void
    on_serv_connected(typhon::tcp::Connector* conn) noexcept {
        xINFO("serv {} connected on {}", conn->id(), ::pthread_self());
    }


    virtual void
    on_serv_disconnected(typhon::tcp::Connector* conn) noexcept {
        xINFO("serv {} disconnected on {}", conn->id(), ::pthread_self());
        server_->notify_serv_disconnected(conn->id());
    }


private:
    typhon::Server* server_ { nullptr };
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

    xINFO("kcp echo listening on 0.0.0.0:5555 (XDP on {}, kcp bpf = {}, envelope bpf = {})", ifname, kcp_bpf_path, envelope_bpf_path);
    server.run();

    ::exit(EXIT_SUCCESS);
}
