#ifndef __TYPHON_KCP_CONFIG_HPP__
#define __TYPHON_KCP_CONFIG_HPP__


#include <inttypes.h>
#include "core/typhon.in.hpp"
#include "utils/cryptor.hpp"
#include "utils/string_ex.hpp"
#include "utils/etcd.hpp"
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
    load(const char* cfname) {
        auto root = YAML::LoadFile(cfname);
        if (!root["server"] || server_.from_yaml(root["server"]) < 0) {
            xFATAL("config.server is invalid");
        }

        if (!root["etcd"] || etcd_.from_yaml(root["etcd"]) < 0) {
            xFATAL("config.etcd is invalid");
        }

        if (root["ifname"]) {
            ifname_ = root["ifname"].as<std::string>();
        }

        if (root["kcp_bpf_path"]) {
            kcp_bpf_path_ = root["kcp_bpf_path"].as<std::string>();
        }

        if (root["envelope_bpf_path"]) {
            envelope_bpf_path_ = root["envelope_bpf_path"].as<std::string>();
        }

        if (!root["kcp"]) {
            xFATAL("config.kcp is invalid");
        }

        auto k = root["kcp"];
        
        if (k["sndbuf"]) {
            sndbuf_ = k["sndbuf"].as<int>();
        }

        if (k["rcvbuf"]) {
            rcvbuf_ = k["rcvbuf"].as<int>();
        }
        
        if (k["sndwnd"]) {
            sndwnd_ = k["sndwnd"].as<int>();
        }

        if (k["rcvwnd"]) {
           rcvwnd_ = k["rcvwnd"].as<int>();
        }

        if (k["nodelay"]) {
            nodelay_ = k["nodelay"].as<int>();
        }

        if (k["interval"]) {
            interval_ = k["interval"].as<int>();
        }

        if (k["resend"]) {
            resend_ = k["resend"].as<int>();
        }

        if (k["nc"]) {
            nc_ = k["nc"].as<int>();
        }

        if (k["siphash"]) {
            auto s = k["siphash"].as<std::string>();
            ASSERT(s.length() == sizeof(SipKey), "无效的 siphash");
            ::memcpy(siphash_, (uint8_t*)s.data(), sizeof(SipKey));
        }

        if (k["x25519_pk"]) {
            auto s = k["x25519_pk"].as<std::string>();
            size_t len = sizeof(X25519Key);
            ASSERT(utils::base64_decode(s, x25519_pk_, &len) == 0 && len == sizeof(X25519Key), "无效的 x25519_pk");
        }

        if (k["x25519_sk"]) {
            auto s = k["x25519_sk"].as<std::string>();
            size_t len = sizeof(X25519Key);
            ASSERT(utils::base64_decode(s, x25519_sk_, &len) == 0 && len == sizeof(X25519Key), "无效的 x25519_sk");
        }

        if (k["ed25519_pk"]) {
            auto s = k["ed25519_pk"].as<std::string>();
            size_t len = sizeof(ED25519PK);
            ASSERT(utils::base64_decode(s, ed25519_pk_, &len) == 0 && len == sizeof(ED25519PK), "无效的 ed25519_pk");
        }

        // 全部校验在 load 内完成 (不再单独提供 check)
        ASSERT(sndbuf_ > 0, "sndbuf is invalid");
        ASSERT(rcvbuf_ > 0, "rcvbuf is invalid");
        ASSERT(sndwnd_ > 0 && sndwnd_ <= 128, "sndwnd is invalid");
        ASSERT(rcvwnd_ > 0 && rcvwnd_ <= 128, "rcvwnd is invalid");
        ASSERT(nodelay_ == 0 || nodelay_ == 1, "nodelay is invalid");
        ASSERT(interval_ >= 10, "interval is invalid");
        ASSERT(resend_ > 0, "resend is invalid");
        ASSERT(nc_ == 0 || nc_ == 1, "nc is invalid");
        ASSERT(!::sodium_is_zero(siphash_, sizeof(siphash_)), "siphash is invalid");
        ASSERT(!::sodium_is_zero(x25519_pk_, sizeof(x25519_pk_)), "x25519_pk is invalid");
        ASSERT(!::sodium_is_zero(x25519_sk_, sizeof(x25519_sk_)), "x25519_sk is invalid");
        ASSERT(!::sodium_is_zero(ed25519_pk_, sizeof(ed25519_pk_)), "ed25519_pk is invalid");
    }


    /**
     * @brief 发送缓冲区大小
     */
    int
    sndbuf() const noexcept {
        return sndbuf_;
    }


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


    /**
     * @brief 协议密钥 (16 字节)
     *        生产部署请修改默认值, 确保安全性
     *        该密钥用于加密协议头, 防止被攻击者轻易伪造数据包
     */
    const SipKey&
    siphash() const noexcept {
        return siphash_;
    }


    const X25519Key&
    x25519_pk() const noexcept {
        return x25519_pk_;
    }


    const X25519Key&
    x25519_sk() const noexcept {
        return x25519_sk_;
    }


    const ED25519PK&
    ed25519_pk() const noexcept {
        return ed25519_pk_;
    }


    const typhon::core::ServerInfo*
    server() const noexcept {
        return &server_;
    }


    const typhon::utils::EtcdConfig*
    etcd() const noexcept {
        return &etcd_;
    }


    std::string
    ifname() const noexcept {
        return ifname_;
    }


    std::string
    kcp_bpf_path() const noexcept {
        return kcp_bpf_path_;
    }


    std::string
    envelope_bpf_path() const noexcept {
        return envelope_bpf_path_;
    }


private:
    explicit
    Conf() noexcept 
    {}


    int               sndbuf_       { 16777216 };   ///< 发送缓冲区大小
    int               rcvbuf_       { 33554432 };   ///< 接收缓冲区大小
    int               sndwnd_       { 128 };        ///< 发送窗口
    int               rcvwnd_       { 128 };        ///< 接收窗口
    int               nodelay_      { 1 };          ///< 是否开启低延迟模式
    int               interval_     { 10 };         ///< update 间隔
    int               resend_       { 3 };          ///< 快速重传, 表示连接跳过3个包的时候就会重传
    int               nc_           { 1 };          ///< 是否关闭拥塞控制, 1为关闭, 0为不关闭s
    SipKey            siphash_      {};             ///< 协议密钥
    X25519Key         x25519_pk_    {};             ///< LOGIN 服务用来作 sealedbox 加密
    X25519Key         x25519_sk_    {};             ///< 用于 鉴权时的 sealedbox 解密
    ED25519PK         ed25519_pk_   {};             ///< LOGIN服务 ed25519 签名公钥, LOGIN服会有私钥签名
    core::ServerInfo  server_;
    utils::EtcdConfig etcd_;
    std::string       ifname_;
    std::string       kcp_bpf_path_;
    std::string       envelope_bpf_path_;
}; // class Conf;

    
} // namespace typhon::kcp


#endif // __TYPHON_KCP_CONFIG_HPP__