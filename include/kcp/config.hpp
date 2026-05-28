#ifndef __TYPHON_KCP_CONFIG_HPP__
#define __TYPHON_KCP_CONFIG_HPP__


#include <inttypes.h>
#include "utils/cryptor.hpp"


namespace typhon::kcp {


class Conf {
    Conf(const Conf&) = delete;
    Conf& operator=(const Conf&) = delete;
    Conf(Conf&&) = delete;
    Conf& operator=(Conf&&) = delete;


public:
    typedef uint8_t SipKey[utils::SIPHASH_KEY_LEN];


    static const Conf*
    instance() noexcept {
        static Conf m;
        return &m;
    }


    ~Conf() noexcept
    {}


    int
    sndbuf() const noexcept {
        return sndbuf_;
    }


    int
    rcvbuf() const noexcept {
        return rcvbuf_;
    }


    int
    sndwnd() const noexcept {
        return sndwnd_;
    }


    int
    rcvwnd() const noexcept {
        return rcvwnd_;
    }


    int
    nodelay() const noexcept {
        return nodelay_;
    }


    int
    interval() const noexcept {
        return interval_;
    }


    int
    resend() const noexcept {
        return resend_;
    }


    int
    nc() const noexcept {
        return nc_;
    }


    uint32_t
    timeout() const noexcept {
        return timeout_;
    }


    const SipKey&
    shkey() const noexcept {
        return shkey_;
    }


private:
    Conf() noexcept {
        uint64_t a[2] = { 0x0102030405060708, 0x090A0B0C0D0E0FAA };
        ::memcpy(shkey_, a, sizeof(a));
    }


    int      sndbuf_   { 1024 * 1024 * 2 };   ///< 发送缓冲区大小
    int      rcvbuf_   { 1024 * 1024 * 4 };   ///< 接收缓冲区大小
    int      sndwnd_   { 128 };               ///< 发送窗口
    int      rcvwnd_   { 128 };               ///< 接收窗口
    int      nodelay_  { 1 };                 ///< 是否开启低延迟模式
    int      interval_ { 10 };                ///< update 间隔
    int      resend_   { 3 };                 ///< 快速重传, 表示连接跳过3个包的时候就会重传
    int      nc_       { 1 };                 ///< 是否关闭拥塞控制, 1为关闭, 0为不关闭
    uint32_t timeout_  { 30000 };             ///< 超时(ms)
    SipKey   shkey_    {};                    ///< 协议密钥
};

    
} // namespace typhon::kcp;


#endif // __TYPHON_KCP_CONFIG_HPP__