#ifndef __ADAM_TCP_SESSION_HPP__
#define __ADAM_TCP_SESSION_HPP__


#include "tcp/buffer.hpp"
#include "core/package.hpp"


namespace adam::tcp {


class Worker;


class Session {
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&) = delete;
    Session& operator=(Session&&) = delete;


public:
    typedef std::shared_ptr<Session> Ptr;


    static Ptr
    create(SOCKET sockfd, Worker* w) noexcept {
        return std::make_shared<Session>(sockfd, w);
    }


    explicit
    Session(SOCKET sockfd, Worker* w) noexcept;


    ~Session() noexcept {
        if (fd_ != INVALID_SOCKET) {
            ::close(fd_);
        }
    }


    /**
     * @brief 是否鉴权
     */
    bool
    authed() const noexcept {
        return id_ != 0;
    }


    uint32_t
    id() const noexcept {
        return id_;
    }


    void
    set_id(uint32_t id) noexcept {
        id_ = id;
    }


    SOCKET
    sockfd() const noexcept {
        return fd_;
    }


    uint64_t
    last_recv_ms() const noexcept {
        return last_recv_ms_;
    }


    const Worker*
    worker() const noexcept {
        return worker_;
    }


    std::string
    remote_addr() const {
        return core::sockaddr_to_string((const sockaddr*)&addr_);
    }


    template<typename T>
    T*
    get_user_data() noexcept {
        return static_cast<T*>(user_data_);
    }


    void
    set_user_data(void* data) noexcept {
        user_data_ = data;
    }


    int
    input(const uint8_t* buf, size_t len) noexcept {
        return rbuf_.append(buf, len);
    }


    /**
     * @brief 从 rbuf_ 分帧 + frame_decode 出一个完整 Package(堆分配, *pk 指向, 调用方 mi_free)。
     * @return  xOK    取到一个完整包, *pke 指向它(host 序)
     * @return  xAGAIN 无数据 / 半包, *pke 不变
     */
    int
    recv(core::Package* pk) noexcept;


    /**
     * @brief 发一个包: 内部 hton(递归翻外层 + 内嵌)成网络序再写; 部分写挂 sbuf_ 等 flush。
     *        入参是 host 序视图 Pke<Host>, 字节序由类型保证, 不再需要调用方手翻内嵌。
     * @return 1 整包发完 / 0 部分或全部排进 sbuf_ / <0 socket 错(EAGAIN 不算)
     */
    ssize_t
    send(core::Package& pk) noexcept;


    /** @brief 纯 flush sbuf_ 残留(EPOLLOUT 触发时续发)。 */
    ssize_t
    send() noexcept;


private:
    uint32_t             id_           { 0 };
    SOCKET               fd_           { INVALID_SOCKET };
    sockaddr_storage     addr_         {};
    socklen_t            addrlen_      { 0 };
    uint64_t             last_recv_ms_ { 0 };
    void*                user_data_    { nullptr };
    Worker*              worker_       { nullptr };
    std::vector<uint8_t> sbuf_         {};
    Buffer               rbuf_         {};
}; // class TcpSession;

    
} // namespace adam::tcp


#endif // __ADAM_TCP_SESSION_HPP__