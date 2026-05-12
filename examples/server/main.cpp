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
main(int argc, char** argv) {
    const char* bpf_path = (argc > 1) ? argv[1] : "kcp.bpf.o";

    typhon::Server server("0.0.0.0:5555", bpf_path);
    g_server = &server;

    ::signal(SIGINT, on_signal);
    ::signal(SIGTERM, on_signal);

    std::cout<<"kcp echo listening on 0.0.0.0:5555 (BPF " << bpf_path << ", Ctrl+C to stop)"<<std::endl;
    server.run();
    exit(0);
}
