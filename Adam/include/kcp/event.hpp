#ifndef __ADAM_KCP_EVENT_HPP__
#define __ADAM_KCP_EVENT_HPP__


#include <memory>


namespace adam::tcp {
    class Connector;
}


namespace adam::kcp {


class Session;
class Server;


class IEvent {
public:
    virtual void on_init(Server*) noexcept {
    }

    virtual void on_stopped(Server*) noexcept {
    }

    virtual int on_sess_connected(std::shared_ptr<Session>) noexcept {
        return 0;
    }

    virtual void on_sess_disconnected(std::shared_ptr<Session>) noexcept {
    }

    virtual void on_serv_connected(std::shared_ptr<tcp::Connector>) noexcept {
    }

    virtual void on_serv_disconnected(std::shared_ptr<tcp::Connector>) noexcept {
    }
}; // namespace IEvent;


} // namespace adam::kcp;


#endif // __ADAM_KCP_EVENT_HPP__