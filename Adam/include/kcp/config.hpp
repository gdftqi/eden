#ifndef __ADAM_KCP_CONFIG_HPP__
#define __ADAM_KCP_CONFIG_HPP__


#include <inttypes.h>
#include "core/adam.in.hpp"
#include "utils/cryptor.hpp"
#include "utils/string_ex.hpp"
#include "utils/etcd.hpp"
#include "utils/log.hpp"


namespace adam::kcp {


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


    /**
     * @brief 从文件中加载 Conf
     */
    void
    load_from_file(const char* fname) noexcept;


    /**
     * @brief 是否生成火焰图
     */
    bool
    flame() const noexcept {
        return flame_;
    }


    /**
     * @brief socket 发送缓冲区大小
     */
    int
    sndbuf() const noexcept {
        return sndbuf_;
    }


    /**
     * @brief socket 接收缓冲区大小
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
     * 
     *        0: 不开启
     * 
     *        1: 开启
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
     * @brief 协议密钥 (16 字节), 该密钥用于加密协议头, 防止被攻击者轻易伪造数据包
     *        
     */
    const SipKey&
    siphash() const noexcept {
        return siphash_;
    }


    /**
     * @brief x25519 公钥 (32 字节), 登录服务用 sealedbox 加密 AccessToken 数据
     */
    const X25519Key&
    x25519_pk() const noexcept {
        return x25519_pk_;
    }


    /**
     * @brief x25519 私钥 (32 字节), 用来解密 AccessToken 数据
     */
    const X25519Key&
    x25519_sk() const noexcept {
        return x25519_sk_;
    }


    /**
     * @brief ed25519pk 用来对 AccessToken 进行签名验证
     */
    const ED25519PK&
    ed25519_pk() const noexcept {
        return ed25519_pk_;
    }


    /**
     * @brief 服务信息
     */
    const adam::core::ServerInfo*
    server() const noexcept {
        return &server_;
    }


    /**
     * @brief ETCD 配置
     */
    const adam::utils::EtcdConfig*
    etcd() const noexcept {
        return &etcd_;
    }


    /**
     * @brief 网卡接口名称, 用于 XDP 验证
     */
    std::string
    ifname() const noexcept {
        return ifname_;
    }


    /**
     * @brief kcp bpf 文件路径
     */
    std::string
    kcp_bpf_path() const noexcept {
        return kcp_bpf_path_;
    }


    /**
     * @brief bpf xdp 文件路径
     */
    std::string
    envelope_bpf_path() const noexcept {
        return envelope_bpf_path_;
    }


    /**
     * @brief 日志文件存放路径
     */
    std::string
    log_path() const noexcept {
        return log_path_;
    }


private:
    explicit
    Conf() noexcept 
    {}


    bool              flame_        { false };
    int               sndbuf_       { 16777216 };   ///< 发送缓冲区大小
    int               rcvbuf_       { 33554432 };   ///< 接收缓冲区大小
    int               sndwnd_       { 128 };        ///< 发送窗口
    int               rcvwnd_       { 128 };        ///< 接收窗口
    int               nodelay_      { 1 };          ///< 是否开启低延迟模式
    int               interval_     { 10 };         ///< update 间隔
    int               resend_       { 3 };          ///< 快速重传, 表示连接跳过3个包的时候就会重传
    int               nc_           { 1 };          ///< 是否关闭拥塞控制, 1为关闭, 0为不关闭ss
    SipKey            siphash_      {};             ///< 协议密钥
    X25519Key         x25519_pk_    {};             ///< LOGIN 服务用来作 sealedbox 加密
    X25519Key         x25519_sk_    {};             ///< 用于 鉴权时的 sealedbox 解密
    ED25519PK         ed25519_pk_   {};             ///< LOGIN服务 ed25519 签名公钥, LOGIN服会有私钥签名
    core::ServerInfo  server_;
    utils::EtcdConfig etcd_;
    std::string       ifname_;
    std::string       kcp_bpf_path_;
    std::string       envelope_bpf_path_;
    std::string       log_path_;
}; // class Conf;

    
} // namespace adam::kcp


#endif // __ADAM_KCP_CONFIG_HPP__