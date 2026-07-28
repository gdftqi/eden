#ifndef __MOSES_HPP__
#define __MOSES_HPP__


#include "adam.h"


class Moses: public adam::kcp::IHook {
public:
    virtual void
    on_init(adam::kcp::Server* s) noexcept override {
        server_ = s;
        xINFO("kcp echo listening on {}", adam::kcp::Conf::instance()->server()->host);
    }


    virtual void
    on_stopped(adam::kcp::Server*) noexcept override {
        xINFO("{} stopped", adam::kcp::Conf::instance()->server()->host);
    }


    virtual int
    on_sess_connected(adam::kcp::Session::Ptr s) noexcept override {
        xINFO("{} connected", s->to_json());
        return 0;
    }


    virtual void
    on_sess_disconnected(adam::kcp::Session::Ptr s) noexcept override {
        xINFO("{} disconnected.", s->to_json());
    }


    virtual void
    on_serv_registed(adam::tcp::Connector::Ptr conn) noexcept override {
        xINFO("backend {} has registed", conn->id());
    }


    virtual void
    on_serv_unregisted(adam::tcp::Connector::Ptr conn) noexcept override {
        xINFO("backend {} has unregisted", conn->id());
    }


    virtual void
    on_terminal_binded(adam::kcp::Session::Ptr s, uint32_t id) noexcept override {
        xINFO("terminal {} has binded to backend {}", s->uid(), id);
    }


    virtual void
    on_terminal_unbinded(adam::kcp::Session::Ptr s, uint32_t id) noexcept override {
        xINFO("terminal {} has unbinded from backend {}", s->uid(), id);
    }
private:
    adam::kcp::Server* server_ { nullptr };
}; // class Moses;


#endif // __MOSES_HPP__
