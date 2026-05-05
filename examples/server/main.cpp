#include <csignal>
#include <iostream>

#include "kcp_server.hpp"


static typhon::UdpServer* g_server = nullptr;


static void
on_signal(int) {
    if (g_server) {
        g_server->stop();
    }
}


int
main(int, char**) {
    typhon::UdpServer server("0.0.0.0:5555");
    g_server = &server;

    ::signal(SIGINT, on_signal);
    ::signal(SIGTERM, on_signal);

    std::cout<<"kcp echo listening on 0.0.0.0:5555 (Ctrl+C to stop)"<<std::endl;
    int err = server.run();
    std::cout<<"server stopped, run() returned "<<err<<std::endl;
    return err;
}
