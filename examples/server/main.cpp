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
        std::cout<<server->host()<<" running on "<<std::this_thread::get_id()<<std::endl;
        return 0;
    }


    virtual void
    on_stopped(typhon::KcpServer* server) noexcept {
        std::cout<<server->host()<<" stopped on "<<std::this_thread::get_id()<<std::endl;
    }


    virtual int
    on_connected(typhon::Kcp::Ptr kcp) noexcept {
        std::cout<<kcp->remote_addr()<<" connected"<<std::endl;
        return 0;
    }


    virtual void
    on_disconnected(typhon::Kcp::Ptr kcp) noexcept {
        std::cout<<kcp->remote_addr()<<" disconnected"<<std::endl;
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
    const char* bpf_path = (argc > 1) ? argv[1] : "kcp.bpf.o";

    EchoService echo;
    typhon::TyService server(&echo, "0.0.0.0:5555", bpf_path);
    g_server = &server;

    ::signal(SIGINT, on_signal);
    ::signal(SIGTERM, on_signal);

    std::cout<<"kcp echo listening on 0.0.0.0:5555 (BPF " << bpf_path << ", Ctrl+C to stop)"<<std::endl;
    server.run();
    exit(0);
}
