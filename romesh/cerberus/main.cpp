#include <csignal>
#include <iostream>

#include "cerberus.hpp"


static std::unique_ptr<Cerberus> server;


static void
on_signal(int) {
    if (server) {
        server->stop();
    }
}


class ServiceEvent: public typhon::kcp::IEvent {
public:
    virtual void
    on_init(void* arg) noexcept {
        server_ = (Cerberus*)arg;
        xINFO("kcp echo listening on {}", Conf::instance()->server()->host);
    }


    virtual void
    on_stopped(void*) noexcept {
        xINFO("{} stopped", Conf::instance()->server()->host);
    }


    virtual int
    on_sess_connected(typhon::kcp::Session::Ptr s) noexcept {
        xINFO("{} connected", s->to_json());
        return 0;
    }


    virtual void
    on_sess_disconnected(typhon::kcp::Session::Ptr s) noexcept {
        xINFO("{} disconnected.", s->to_json());
    }


    virtual void
    on_user_connected(typhon::kcp::Session::Ptr s) noexcept {
        xINFO("{} connected.", s->to_json());
        server_->notify_user_connected(s->user_id());
    }


    virtual void
    on_user_disconnected(typhon::kcp::Session::Ptr s) noexcept {
        xINFO("[{}:{}] unauth on {}", s->user_id(), s->remote_addr(), ::pthread_self());
        server_->notify_user_disconnected(s->user_id());
    }


    virtual void
    on_serv_connected(typhon::tcp::Connector::Ptr conn) noexcept {
        xINFO("serv {} connected on {}", conn->id(), ::pthread_self());
    }


    virtual void
    on_serv_disconnected(typhon::tcp::Connector::Ptr conn) noexcept {
        xINFO("serv {} disconnected on {}", conn->id(), ::pthread_self());
        server_->notify_serv_disconnected(conn->id());
    }
private:
    Cerberus* server_ { nullptr };
}; // class ServiceEvent;


int
main(int, char**) {
    if (!typhon::utils::lock_pid("server.pid")) {
        xERROR("程序已启动");
        ::exit(EXIT_FAILURE);
    }

    Conf::instance()->load("config.yml");

    ServiceEvent se;
    server = std::make_unique<Cerberus>(&se);

    ::signal(SIGINT, on_signal);
    ::signal(SIGTERM, on_signal);

    server->run();

    xINFO("服务关闭");
    ::exit(EXIT_SUCCESS);
}
