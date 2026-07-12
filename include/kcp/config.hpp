#ifndef __TYPHON_KCP_CONFIG_HPP__
#define __TYPHON_KCP_CONFIG_HPP__


#include <inttypes.h>
#include "utils/cryptor.hpp"
#include "utils/string_ex.hpp"
#include "utils/log.hpp"


namespace typhon::kcp {


/** 
 * @brief KCP 配置类 (单例)
 */
class Conf {
    Conf(const Conf&) = delete;
    Conf& operator=(const Conf&) = delete;
    Conf(Conf&&) = delete;
    Conf& operator=(Conf&&) = delete;


public:
    typedef uint8_t SipKey[utils::SIPHASH_KEY_LEN];
    typedef uint8_t X25519Key[utils::X25519_KEY_LEN];
    typedef uint8_t ED25519PK[utils::ED25519_PK_LEN];


    static Conf*
    instance() noexcept {
        static Conf m;
        return &m;
    }


    ~Conf() noexcept
    {}


    void
    check() const noexcept {
        ASSERT(id_ > 0, "id is invalid");
        ASSERT(sndbuf_ > 0, "sndbuf is invalid");
        ASSERT(rcvbuf_ > 0, "rcvbuf is invalid");
        ASSERT(sndwnd_ > 0 && sndwnd_ <= 128, "sndwnd is invalid"); 
        ASSERT(rcvwnd_ > 0 && rcvwnd_ <= 128, "rcvwnd is invalid");
        ASSERT(nodelay_ == 0 || nodelay_ == 1, "nodelay is invalid");
        ASSERT(interval_ >= 10, "interval is invalid");
        ASSERT(resend_ > 0, "resend is invalid");
        ASSERT(nc_ == 0 || nc_ == 1, "nc is invalid");
        ASSERT(timeout_ > 0, "timeout is invalid");
        ASSERT(!::sodium_is_zero(siphash_, sizeof(siphash_)), "siphash is invalid");
        ASSERT(!::sodium_is_zero(x25519_pk_, sizeof(x25519_pk_)), "x25519_pk is invalid");
        ASSERT(!::sodium_is_zero(x25519_sk_, sizeof(x25519_sk_)), "x25519_sk is invalid");
        ASSERT(!::sodium_is_zero(ed25519_pk_, sizeof(ed25519_pk_)), "ed25519_pk is invalid");
    }


    uint32_t
    id() const noexcept {
        return id_;
    }


    Conf*
    set_id(uint32_t id) noexcept {
        id_ = id;
        return this;
    }


    /**
     * @brief 发送缓冲区大小
     */
    int
    sndbuf() const noexcept {
        return sndbuf_;
    }


    Conf*
    set_sndbuf(int sndbuf) noexcept {
        sndbuf_ = sndbuf;
        return this;
    }


    /**
     * @brief 接收缓冲区大小
     */
    int
    rcvbuf() const noexcept {
        return rcvbuf_;
    }


    Conf*
    set_rcvbuf(int rcvbuf) noexcept {
        rcvbuf_ = rcvbuf;
        return this;
    }


    /**
     * @brief kcp 发送窗口大小
     */
    int
    sndwnd() const noexcept {
        return sndwnd_;
    }


    Conf*
    set_sndwnd(int sndwnd) noexcept {
        sndwnd_ = sndwnd;
        return this;
    }


    /**
     * @brief kcp 接收窗口大小
     */
    int
    rcvwnd() const noexcept {
        return rcvwnd_;
    }


    Conf*
    set_rcvwnd(int rcvwnd) noexcept {
        rcvwnd_ = rcvwnd;
        return this;
    }


    /**
     * @brief 是否开启低延迟模式 (nodelay)
      *        0: 不开启
      *        1: 开启, 等同于 nodelay(1, 10, 2, 1)
      *        2: 开启, 等同于 nodelay(1, 10, 2, 0)
      *        3: 开启, 等同于 nodelay(1, 10, 0, 0)
     */
    int
    nodelay() const noexcept {
        return nodelay_;
    }


    Conf*
    set_nodelay(int nodelay) noexcept {
        nodelay_ = nodelay;
        return this;
    }


    /**
     * @brief kcp update 间隔 (ms)
     */
    int
    interval() const noexcept {
        return interval_;
    }


    Conf*
    set_interval(int interval) noexcept {
        interval_ = interval;
        return this;
    }


    /**
     * @brief kcp 快速重传, 表示连接跳过 resend_ 个包的时候就会重传
     */
    int
    resend() const noexcept {
        return resend_;
    }


    Conf*
    set_resend(int resend) noexcept {
        resend_ = resend;
        return this;
    }


    /**
     * @brief 是否关闭拥塞控制, 1为关闭, 0为不关闭
     */
    int
    nc() const noexcept {
        return nc_;
    }


    Conf*
    set_nc(int nc) noexcept {
        nc_ = nc;
        return this;
    }


    /**
     * @brief kcp 超时 (ms)
     *        连接在 timeout 时间内没有任何数据交互, 就会被 kcp 认为已经断开
     */
    uint64_t
    timeout() const noexcept {
        return timeout_;
    }


    Conf*
    set_timeout(uint64_t timeout) noexcept {
        timeout_ = timeout;
        return this;
    }


    /**
     * @brief 协议密钥 (16 字节)
     *        生产部署请修改默认值, 确保安全性
     *        该密钥用于加密协议头, 防止被攻击者轻易伪造数据包
     */
    const SipKey&
    siphash() const noexcept {
        return siphash_;
    }


    Conf*
    set_siphash(const std::string& s) noexcept {
        ASSERT(s.length() == sizeof(SipKey), "无效的 siphash");
        ::memcpy(siphash_, (uint8_t*)s.data(), sizeof(SipKey));
        return this;
    }


    const X25519Key&
    x25519_pk() const noexcept {
        return x25519_pk_;
    }


    Conf*
    set_x25519_pk(const std::string& s) noexcept {
        ASSERT(s.length() > sizeof(x25519_pk_), "无效的 x25519 公钥");
        size_t len = sizeof(x25519_pk_);
        ASSERT(utils::base64_decode(s, x25519_pk_, &len) == 0, "无效的 x25519 公钥");
        return this;
    }


    const X25519Key&
    x25519_sk() const noexcept {
        return x25519_sk_;
    }


    Conf*
    set_x25519_sk(const std::string& s) noexcept {
        ASSERT(s.length() > sizeof(x25519_sk_), "无效的 x25519 私钥");
        size_t len = sizeof(x25519_sk_);
        ASSERT(utils::base64_decode(s, x25519_sk_, &len) == 0, "无效的 x25519 私钥");
        return this;
    }


    const ED25519PK&
    ed25519_pk() const noexcept {
        return ed25519_pk_;
    }


    Conf*
    set_ed25519_pk(const std::string& s) noexcept {
        ASSERT(s.length() > sizeof(ed25519_pk_), "无效的 ed25519 公钥");
        size_t len = sizeof(ed25519_pk_);
        ASSERT(utils::base64_decode(s, ed25519_pk_, &len) == 0, "无效的 ed25519 公钥");
        return this;
    }


private:
    explicit
    Conf() noexcept 
    {}


    uint32_t  id_           { 0 };
    int       sndbuf_       { 16777216 };   ///< 发送缓冲区大小
    int       rcvbuf_       { 33554432 };   ///< 接收缓冲区大小
    int       sndwnd_       { 128 };        ///< 发送窗口
    int       rcvwnd_       { 128 };        ///< 接收窗口
    int       nodelay_      { 1 };          ///< 是否开启低延迟模式
    int       interval_     { 10 };         ///< update 间隔
    int       resend_       { 3 };          ///< 快速重传, 表示连接跳过3个包的时候就会重传
    int       nc_           { 1 };          ///< 是否关闭拥塞控制, 1为关闭, 0为不关闭
    uint64_t  timeout_      { 45000 };      ///< 超时(ms)
    SipKey    siphash_      {};             ///< 协议密钥
    X25519Key x25519_pk_    {};             ///< LOGIN 服务用来作 sealedbox 加密
    X25519Key x25519_sk_    {};             ///< 用于 鉴权时的 sealedbox 解密
    ED25519PK ed25519_pk_   {};             ///< LOGIN服务 ed25519 签名公钥, LOGIN服会有私钥签名
}; // class Conf;

    
} // namespace typhon::kcp;


#endif // __TYPHON_KCP_CONFIG_HPP__