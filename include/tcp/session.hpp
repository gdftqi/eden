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
        if (fd_ != core::INVALID_SOCKET) {
            ::close(fd_);
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
        return fd_;
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
     * @warning **只翻外层**:函数内部只调 pke_hton(pke),即**原地把 PackageEx 头 10B
     *          翻成网络字节序**。**内嵌 Package 不翻** —— 调用方必须在调 send() 之前
     *          自行保证内嵌 Package 已是网络序(回程 handler 构造完 Package 后调
     *          pk_hton(pke_get_pk(pke)))。漏翻则网关侧 pk_ntoh 读出乱码。
     *          (对比: 网关→后端方向内嵌来自 KCP 透传, 天然网络序, 不必翻; 后端→网关
     *           回程是后端本地构造, host 序, 必须由调用方翻。)
     *
     *          就地副作用 —— 返回后:
     *            - pke 外层字段已是网络序,**不能再当 host 序读**。
     *            - **不允许对同一个 pke 再次调用 send()**(pke_hton 会把它翻回去)。
     *
     *          典型用法是 handler 里「decode → 改内容 → pk_hton 内嵌 → send → 丢弃 pke」,
     *          pke 指向已消费区, 就地翻转无害。
     *
     * @param pke 待发送的 PackageEx,允许传 nullptr(仅 flush sbuf_,不追加新包)。
     * @return  1  整包同步发完(sbuf_ 也已 drain)
     * @return  0  部分/全部内容被排到 sbuf_,等 EPOLLOUT 触发再 flush
     * @return <0  socket 错(EAGAIN 不算),负值是 -errno
     */
    ssize_t
    send(core::PackageEx* pke = nullptr) noexcept;


private:
    bool                 authed_       { false };
    core::SOCKET         fd_           { core::INVALID_SOCKET };
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