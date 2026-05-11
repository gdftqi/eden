#include <csignal>
#include <iostream>

#include "server.hpp"


static typhon::Server* g_server = nullptr;


static void
on_signal(int) {
    if (g_server) {
        g_server->stop();
    }
}


int
main(int, char**) {
    typhon::Server server("0.0.0.0:5555");
    g_server = &server;

    ::signal(SIGINT, on_signal);
    ::signal(SIGTERM, on_signal);

    std::cout<<"kcp echo listening on 0.0.0.0:5555 (Ctrl+C to stop)"<<std::endl;
    server.run();
    exit(0);
}
