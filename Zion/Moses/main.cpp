#include <csignal>
#include <gperftools/profiler.h>

#include "kcp/server.hpp"


using adam::kcp::Conf;


static std::unique_ptr<adam::kcp::Server> server;


static void
on_signal(int) {
    if (server) {
        server->stop();
    }
}


class ServiceEvent: public adam::kcp::IEvent {
public:
    virtual void
    on_init(adam::kcp::Server* s) noexcept {
        server_ = s;
        xINFO("kcp echo listening on {}", Conf::instance()->server()->host);
    }


    virtual void
    on_stopped(adam::kcp::Server*) noexcept {
        xINFO("{} stopped", Conf::instance()->server()->host);
    }


    virtual int
    on_sess_connected(adam::kcp::Session::Ptr s) noexcept {
        xINFO("{} connected", s->to_json());
        return 0;
    }


    virtual void
    on_sess_disconnected(adam::kcp::Session::Ptr s) noexcept {
        xINFO("{} disconnected.", s->to_json());
    }


    virtual void
    on_serv_connected(adam::tcp::Connector::Ptr conn) noexcept {
        xINFO("serv {} connected on {}", conn->id(), ::pthread_self());
    }


    virtual void
    on_serv_disconnected(adam::tcp::Connector::Ptr conn) noexcept {
        xINFO("serv {} disconnected on {}", conn->id(), ::pthread_self());
    }
private:
    adam::kcp::Server* server_ { nullptr };
}; // class ServiceEvent;


int
main(int, char**) {
    if (!adam::utils::lock_pid("moses.pid")) {
        xERROR("程序已启动");
        ::exit(EXIT_FAILURE);
    }

    Conf::instance()->load_from_file("config.yml");

    if (Conf::instance()->log_path().length() > 0) {
        adam::utils::init_log(Conf::instance()->log_path());
    }

    if (Conf::instance()->flame()) {
        ProfilerStart("./moses.prof");
    }

    ServiceEvent se;
    server = std::make_unique<adam::kcp::Server>(&se);

    ::signal(SIGINT, on_signal);
    ::signal(SIGTERM, on_signal);

    server->run();

    xINFO("服务关闭");

    if (Conf::instance()->flame()) {
        ProfilerStop();
    }
    ::exit(EXIT_SUCCESS);
}
