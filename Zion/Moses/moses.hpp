#ifndef __MOSES_HPP__
#define __MOSES_HPP__


#include "adam.h"


class Moses: public adam::kcp::IHook {
public:
    virtual void
    on_init(adam::kcp::Server* s) noexcept {
        server_ = s;
        xINFO("kcp echo listening on {}", adam::kcp::Conf::instance()->server()->host);
    }


    virtual void
    on_stopped(adam::kcp::Server*) noexcept {
        xINFO("{} stopped", adam::kcp::Conf::instance()->server()->host);
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
}; // class Moses;


#endif // __MOSES_HPP__
