#ifndef __CCS_HPP__
#define __CCS_HPP__


#include "adam.h"


class CCS: public adam::tcp::Server::IHook {
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
    on_sess_connected(adam::tcp::Session::Ptr sess) noexcept override {
        xINFO("{} connected", sess->remote_addr());
        return 0;
    }


    void
    on_sess_disconnected(adam::tcp::Session::Ptr sess) noexcept override {
        xINFO("{} disconnected", sess->remote_addr());
    }


    virtual void
    on_serv_registed(adam::tcp::Session::Ptr s) noexcept override {
        xINFO("{} has registed", s->id());
    }


    virtual void
    on_serv_unregisted(adam::tcp::Session::Ptr s) noexcept override {
        xINFO("{} has unregisted", s->id());
    }


    virtual int
    on_terminal_enter(adam::tcp::Terminal::Ptr t) noexcept override {
        xINFO("{} has enter", t->uid());
        return 0;
    }


    virtual void
    on_terminal_leave(adam::tcp::Terminal::Ptr t, uint32_t code) noexcept override {
        xINFO("{} has leave => code = {}", t->uid(), code);
    }
}; // class CCS;


#endif // __CCS_HPP__