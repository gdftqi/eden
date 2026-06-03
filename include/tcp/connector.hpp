#ifndef __TYPHON_TCP_CONNECTOR_HPP__
#define __TYPHON_TCP_CONNECTOR_HPP__


#include "core/typhon.in.hpp"
#include "tcp/config.hpp"


namespace typhon::tcp {


class Connector {
    Connector(const Connector&) = delete;
    Connector& operator=(const Connector&) = delete;
    Connector(Connector&&) = delete;
    Connector& operator=(Connector&&) = delete;


public:
    typedef std::shared_ptr<Connector> Ptr;
    

    static Ptr
    create(uint32_t id, const char* host) noexcept {
        return std::make_shared<Connector>(id, host);
    }


    explicit
    Connector(uint32_t id, const char* host) noexcept
        : id_(id), host_(host)
    {}


    ~Connector() noexcept {
        if (cfd_ != core::INVALID_SOCKET) {
            ::close(cfd_);
        }
    }


    core::SOCKET
    fd() const noexcept {
        return cfd_;
    }


    int
    connect() noexcept {
        if (cfd_ == core::INVALID_SOCKET) {
            cfd_ = core::tcp_connect(host_.c_str(), Conf::instance()->sndbuf(), Conf::instance()->rcvbuf());
            if (cfd_ < 0) {
                int err = errno;
                cfd_ = core::INVALID_SOCKET;
                return -err;
            }
        }

        return 0;
    }


private:
    uint32_t     id_    { 0 };
    core::SOCKET cfd_   { core::INVALID_SOCKET };
    std::string  host_;
}; // class Connector;

    
} // namespace typhon::tcp;


#endif // __TYPHON_TCP_CONNECTOR_HPP__