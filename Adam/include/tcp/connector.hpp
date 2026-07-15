#ifndef __ADAM_TCP_CONNECTOR_HPP__
#define __ADAM_TCP_CONNECTOR_HPP__


#include "core/adam.in.hpp"
#include "core/buffer.hpp"
#include "tcp/config.hpp"


namespace adam::tcp {


class Connector {
    Connector(const Connector&) = delete;
    Connector& operator=(const Connector&) = delete;
    Connector(Connector&&) = delete;
    Connector& operator=(Connector&&) = delete;


public:
    typedef std::shared_ptr<Connector> Ptr;


    enum class State : uint8_t {
        Disconnected,
        Connecting,
        Connected,
    };


    static Ptr
    create(uint32_t id, const char* host) noexcept {
        auto cfd = core::tcp_connect(host);
        if (cfd < 0) {
            int err = errno;
            cfd = core::INVALID_SOCKET;
            xERROR("创建后端连接失败: id = {}, host = {}, err = {}, errstr = {}", id, host, err, ::strerror(err));
            return nullptr;
        }
        return std::make_shared<Connector>(id, cfd, host);
    }


    explicit
    Connector(uint32_t id, core::SOCKET fd, const char* host) noexcept
        : id_(id)
        , fd_(fd)
        , host_(host)
        , desc_(std::format("[{}:{}]", id, host_)) {
        last_recv_ms_ = last_send_ms_ = utils::systime_ms();
        state_ = State::Connecting;
    }


    ~Connector() noexcept {
        if (fd_ != core::INVALID_SOCKET) {
            ::close(fd_);
        }
    }


    core::SOCKET
    fd() const noexcept {
        return fd_;
    }


    std::string
    to_string() const noexcept {
        return desc_;
    }


    bool
    authed() const noexcept {
        return authed_;
    }


    void
    set_authed(bool authed) noexcept {
        authed_ = authed;
    }


    uint32_t
    id() const noexcept {
        return id_;
    }


    const std::string&
    host() const noexcept {
        return host_;
    }


    State
    state() const noexcept {
        return state_;
    }


    bool
    is_connected() const noexcept {
        return state_ == State::Connected;
    }


    void
    set_state(State s) noexcept {
        state_ = s;
    }


    ssize_t
    send(core::Package& pk, uint64_t now) noexcept;


    ssize_t
    send(uint64_t now) noexcept;


    int
    input(const uint8_t* data, size_t len) noexcept {
        return rbuf_.append(data, len);
    }


    int
    recv(core::Package** pk, uint64_t now) noexcept;


    int
    update(uint64_t now) noexcept;


    int
    regist(uint32_t id, uint64_t now) noexcept;


private:
    bool         authed_       { false };
    uint32_t     id_           { 0 };
    core::SOCKET fd_           { core::INVALID_SOCKET };
    State        state_        { State::Disconnected };
    uint64_t     last_recv_ms_ { 0 };
    uint64_t     last_send_ms_ { 0 };
    std::string  host_;
    std::string  desc_;
    core::Buffer rbuf_;
    core::Buffer sbuf_;
}; // class Connector;

    
} // namespace adam::tcp;


#endif // __ADAM_TCP_CONNECTOR_HPP__