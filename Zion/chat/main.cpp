#include <csignal>
#include <cstdlib>

#include "tcp/server.hpp"
#include "utils/sys.hpp"


static adam::tcp::Server* g_server = nullptr;


static void
on_signal(int) {
    if (g_server) {
        g_server->stop();
    }
}


class EchoService: public adam::tcp::Server::IEvent {
public:
    void
    on_init(adam::tcp::Server* s) noexcept override {
        xINFO("{} running on {}", s->host(), ::pthread_self());
    }


    void
    on_stopped(adam::tcp::Server* s) noexcept override {
        xINFO("{} stopped on {}", s->host(), ::pthread_self());
    }


    int
    on_connected(adam::tcp::Session::Ptr sess) noexcept override {
        xINFO("{} connected on {}", sess->remote_addr(), ::pthread_self());
        return 0;
    }


    void
    on_disconnected(adam::tcp::Session::Ptr sess) noexcept override {
        xINFO("{} disconnected on {}", sess->remote_addr(), ::pthread_self());
    }
};


/**
 * @brief echo handler:把收到的 Package 原样回送给对端。
 *        对应 pk_id = 1(PING)消息。
 */
static void
echo_handler(adam::tcp::Session::Ptr sess, adam::core::Package* pk) noexcept {
    // src/dst 对调, 原样回送给对端
    uint32_t src = pk->data.src_id;
    pk->data.src_id = pk->data.dst_id;
    pk->data.dst_id = src;

    int rc = sess->send(*pk);
    if (rc < 0) {
        xWARN("echo send failed: fd={}, rc={}", sess->sockfd(), rc);
    }
}


int
main(int /*argc*/, char** /*argv*/) {
    if (!adam::utils::lock_pid("tcp_echo.pid")) {
        xERROR("tcp_echo 已经在运行");
        return EXIT_FAILURE;
    }

    adam::tcp::Conf* conf = adam::tcp::Conf::instance();

    conf->load("config.yml");

    EchoService service;
    adam::tcp::Server server(conf->server()->host.c_str(), &service);
    g_server = &server;

    // PK_ID_PING = 1,跟 test_tcp.py 一致
    server.regist_handler(1, &echo_handler);

    ::signal(SIGINT,  on_signal);
    ::signal(SIGTERM, on_signal);

    server.run();
    return EXIT_SUCCESS;
}
