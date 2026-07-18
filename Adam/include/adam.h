#ifndef __ADAM_H__
#define __ADAM_H__


#include <csignal>
#include "kcp/server.hpp"
#include "tcp/server.hpp"
#include "utils/prof.hpp"


using KcpConf = adam::kcp::Conf;
using TcpConf = adam::tcp::Conf;


#define KCP_PK_HANDLE(id, fn)  server->regist_handler(id, &fn);


#define KCP_SERVER_MAIN(name, config, ...) \
    static adam::kcp::Server::Ptr server; static void on_signal(int) { if (server) server->stop(); } \
    int main(int /* argc */, char** /* argv */) { \
        if (!adam::utils::lock_pid(#name ".pid")) { xERROR("程序已启动"); return EXIT_FAILURE; } \
        KcpConf::instance()->load_from_file(config); \
        adam::utils::init_log(KcpConf::instance()->log_path()); \
        adam::utils::Prof prof(KcpConf::instance()->prof_path()); \
        name se; server = std::make_unique<adam::kcp::Server>(&se); \
        __VA_ARGS__ \
        ::signal(SIGINT, on_signal); ::signal(SIGTERM, on_signal); \
        server->run(); xINFO("服务关闭"); return EXIT_SUCCESS; \
    }


#endif // __ADAM_H__
