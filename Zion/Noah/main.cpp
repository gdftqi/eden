#include <csignal>
#include <cstdlib>

#include "tcp/server.hpp"
#include "utils/sys.hpp"
#include "utils/prof.hpp"


static std::unique_ptr<adam::tcp::Server> server;

using adam::tcp::Conf;


static void
on_signal(int) {
    if (server) {
        server->stop();
    }
}


class Noah: public adam::tcp::Server::IHook {
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
        xWARN("echo send failed: fd={}, rc={}", sess->fd(), rc);
    }
}


int
main(int /*argc*/, char** /*argv*/) {
    if (!adam::utils::lock_pid("noah.pid")) {
        xERROR("noah 已经在运行");
        return EXIT_FAILURE;
    }

    Conf::instance()->load_from_file("config.yml");
    adam::utils::init_log(Conf::instance()->log_path());
    adam::utils::Prof prof(Conf::instance()->prof_path());

    Noah s;
    server = std::make_unique<adam::tcp::Server>(Conf::instance()->server()->host.c_str(), &s);

    // PK_ID_PING = 1,跟 test_tcp.py 一致
    server->regist_handler(1, &echo_handler);

    ::signal(SIGINT,  on_signal);
    ::signal(SIGTERM, on_signal);

    server->run();

    xINFO("服务关闭");
    return EXIT_SUCCESS;
}
