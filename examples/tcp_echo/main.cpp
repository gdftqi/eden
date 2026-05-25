#include <csignal>
#include <cstdlib>

#include "tcp/server.hpp"
#include "utils/sys.hpp"


static typhon::tcp::Server* g_server = nullptr;


static void
on_signal(int) {
    if (g_server) {
        g_server->stop();
    }
}


class EchoService: public typhon::tcp::Server::IEvent {
public:
    void
    on_init(typhon::tcp::Server* s) noexcept override {
        xINFO("{} running on {}", s->host(), ::pthread_self());
    }


    void
    on_stopped(typhon::tcp::Server* s) noexcept override {
        xINFO("{} stopped on {}", s->host(), ::pthread_self());
    }


    int
    on_connected(typhon::tcp::Session::Ptr sess) noexcept override {
        xINFO("{} connected on {}", sess->remote_addr(), ::pthread_self());
        return 0;
    }


    void
    on_disconnected(typhon::tcp::Session::Ptr sess) noexcept override {
        xINFO("{} disconnected on {}", sess->remote_addr(), ::pthread_self());
    }
};


/**
 * @brief echo handler:把收到的 Package 原样回送给对端。
 *        对应 pk_id = 1(PING)消息。
 */
static void
echo_handler(typhon::tcp::Session::Ptr sess, const typhon::core::PackageEx* pke) noexcept {
    // Session::send 接收非 const Package*(因为内部要 pk_hton 翻字节序)。
    // 这里直接复用入站 buffer,handler 返回后 worker 才会推进 PkgBuf 游标,
    // 在此期间 buffer 不会被覆盖.
    int rc = sess->send(const_cast<typhon::core::PackageEx*>(pke));
    if (rc < 0) {
        xWARN("echo send failed: fd={}, rc={}", sess->sockfd(), rc);
    }
}


int
main(int /*argc*/, char** /*argv*/) {
    if (!typhon::utils::lock_pid("tcp_echo.pid")) {
        xERROR("tcp_echo 已经在运行");
        return EXIT_FAILURE;
    }

    EchoService service;
    typhon::tcp::Server server("0.0.0.0:6688", &service);
    g_server = &server;

    // PK_ID_PING = 1,跟 test_tcp.py 一致
    server.regist_handler(1, &echo_handler);

    ::signal(SIGINT,  on_signal);
    ::signal(SIGTERM, on_signal);

    server.run();
    return EXIT_SUCCESS;
}
