#ifndef __TYPHON_TCP_SESSION_HPP__
#define __TYPHON_TCP_SESSION_HPP__


#include "core/buffer.hpp"
#include "core/package.hpp"


namespace typhon::tcp {


class Proc;


class Session {
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&) = delete;
    Session& operator=(Session&&) = delete;


public:
    typedef std::shared_ptr<Session> Ptr;


    static Ptr
    create(core::SOCKET sockfd, Proc* w) noexcept {
        return std::make_shared<Session>(sockfd, w);
    }


    explicit
    Session(core::SOCKET sockfd, Proc* w) noexcept;


    ~Session() noexcept {
        if (sockfd_ != core::INVALID_SOCKET) {
            ::close(sockfd_);
        }
    }


    /**
     * @brief 是否鉴权
     */
    bool
    authed() const noexcept {
        return authed_;
    }


    core::SOCKET
    sockfd() const noexcept {
        return sockfd_;
    }


    uint64_t
    last_recv_ms() const noexcept {
        return last_recv_ms_;
    }


    const Proc*
    proc() const noexcept {
        return proc_;
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


    bool
    input(const uint8_t* buf, size_t len) noexcept {
        return rbuf_.append(buf, len);
    }


    /**
     * @brief 从 pbuf_ 里 decode 出一个 PackageEx,并更新 last_recv_ms_。
     * @return  1 成功 decode 出一个完整 PackageEx, *pke 已指向该包
     * @return  0 pbuf_ 中没有足够数据 decode 出一个完整 PackageEx, *pke 不变
     * @return <0 decode 失败, -errno, *pke 不变
     */
    int
    recv(core::PackageEx** pke) noexcept;


    /**
     * @brief 把 PackageEx 发给对端;若内核 buffer 满则把剩余字节挂到 sbuf_ 等下次 flush。
     *
     * @warning **就地副作用**:函数内部会先后调 pke_hton(pke) + pk_hton(内嵌 Package),
     *          即**原地把 PackageEx 头 10B + 内嵌 Package 头 10B 共 20B 翻成网络字节序**。
     *          返回后:
     *            - pke 指向的字段(含内嵌 Package)已经是网络序,**不能再当 host 序读**。
     *            - **不允许对同一个 pke 再次调用 send()** —— 会被 hton 翻回去,
     *              下游收到字节序错乱的包。
     *            - 调用方需要拷贝的应是 pke 之前的 host 序字段值,不是 pke 本身。
     *
     *          典型用法是 handler 里「decode 出来 → 立即 send → 丢弃 pke」,pke 指向
     *          PkgBuf 已消费区,decode 已经推过 rpos,后续没人再读这段字节,
     *          就地翻转无害。
     *
     * @param pke 待发送的 PackageEx,允许传 nullptr(仅 flush sbuf_,不追加新包)。
     * @return  1  整包同步发完(sbuf_ 也已 drain)
     * @return  0  部分/全部内容被排到 sbuf_,等 EPOLLOUT 触发再 flush
     * @return <0  socket 错(EAGAIN 不算),负值是 -errno
     */
    int
    send(core::PackageEx* pke) noexcept;


private:
    int
    send(const uint8_t* data, size_t len) noexcept;


    bool                 authed_       { false };
    core::SOCKET         sockfd_       { core::INVALID_SOCKET };
    sockaddr_storage     addr_         {};
    socklen_t            addrlen_      { 0 };
    uint64_t             last_recv_ms_ { 0 };
    void*                user_data_    { nullptr };
    Proc*                proc_         { nullptr };
    std::vector<uint8_t> sbuf_         {};
    core::RcvBuf         rbuf_         {};
}; // class TcpSession;

    
} // namespace typhon::tcp


#endif // __TYPHON_TCP_SESSION_HPP__