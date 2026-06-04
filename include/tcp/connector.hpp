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


    /**
     * @brief 连接状态机。非阻塞 connect 不会一次连上,要经历 Connecting → Connected。
     *   - Disconnected: 未发起 / 已断开
     *   - Connecting:   connect() 已发起(socket 非阻塞,EINPROGRESS),等 EPOLLOUT 确认
     *   - Connected:    EPOLLOUT 触发后 getsockopt(SO_ERROR)==0,可正常收发
     */
    enum class State : uint8_t {
        Disconnected,
        Connecting,
        Connected,
    };


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


    void
    set_state(State s) noexcept {
        state_ = s;
    }


    /**
     * @brief 发起非阻塞连接。socket 在 tcp_connect 里已设 O_NONBLOCK,
     *        所以这里**不阻塞** —— 连接通常返回 EINPROGRESS(tcp_connect 已把它当
     *        正常处理,返回有效 fd)。返回后状态置 Connecting,调用方需把 fd 加进 epoll
     *        监听 EPOLLOUT,在连接完成时 getsockopt(SO_ERROR) 确认再转 Connected。
     * @return  0  发起成功(fd 有效, 状态 Connecting; 可能已连上, 由 EPOLLOUT 确认)
     * @return <0  发起失败(-errno)
     */
    int
    connect() noexcept {
        if (cfd_ == core::INVALID_SOCKET) {
            cfd_ = core::tcp_connect(host_.c_str(), Conf::instance()->sndbuf(), Conf::instance()->rcvbuf());
            if (cfd_ < 0) {
                int err = errno;
                cfd_ = core::INVALID_SOCKET;
                return -err;
            }
            state_ = State::Connecting;
        }

        return 0;
    }


private:
    uint32_t     id_    { 0 };
    core::SOCKET cfd_   { core::INVALID_SOCKET };
    State        state_ { State::Disconnected };
    std::string  host_;
}; // class Connector;

    
} // namespace typhon::tcp;


#endif // __TYPHON_TCP_CONNECTOR_HPP__