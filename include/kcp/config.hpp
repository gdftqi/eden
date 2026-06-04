#ifndef __TYPHON_KCP_CONFIG_HPP__
#define __TYPHON_KCP_CONFIG_HPP__


#include <inttypes.h>
#include "utils/cryptor.hpp"


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


    static const Conf*
    instance() noexcept {
        static Conf m;
        return &m;
    }


    ~Conf() noexcept
    {}


    /**
     * @brief 发送缓冲区大小
     */
    int
    sndbuf() const noexcept {
        return sndbuf_;
    }


    /**
     * @brief 接收缓冲区大小
     */
    int
    rcvbuf() const noexcept {
        return rcvbuf_;
    }


    /**
     * @brief kcp 发送窗口大小
     */
    int
    sndwnd() const noexcept {
        return sndwnd_;
    }


    /**
     * @brief kcp 接收窗口大小
     */
    int
    rcvwnd() const noexcept {
        return rcvwnd_;
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


    /**
     * @brief kcp update 间隔 (ms)
     */
    int
    interval() const noexcept {
        return interval_;
    }

    /**
     * @brief kcp 快速重传, 表示连接跳过 resend_ 个包的时候就会重传
     */
    int
    resend() const noexcept {
        return resend_;
    }


    /**
     * @brief 是否关闭拥塞控制, 1为关闭, 0为不关闭
     */
    int
    nc() const noexcept {
        return nc_;
    }


    /**
     * @brief kcp 超时 (ms)
     *        连接在 timeout 时间内没有任何数据交互, 就会被 kcp 认为已经断开
     */
    uint32_t
    timeout() const noexcept {
        return timeout_;
    }


    /**
     * @brief 协议密钥 (16 字节)
     *        生产部署请修改默认值, 确保安全性
     *        该密钥用于加密协议头, 防止被攻击者轻易伪造数据包
     */
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