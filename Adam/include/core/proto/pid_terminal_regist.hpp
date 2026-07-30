#ifndef __ADAM_CORE_PROTO_PID_TER_REG_HPP__
#define __ADAM_CORE_PROTO_PID_TER_REG_HPP__


#include <cinttypes>
#include <cstddef>
#include "utils/cryptor.hpp"


namespace adam::core {


/**
 * @brief 端终注册请求. 对应 Eva 中的结构是 AccessToken
 */
struct RegistTerminalReq {
    static constexpr int LEN = 116;


    /**
     * @brief 被 ed25519 签名的前段(expire..cli_pk)
     */
    static constexpr int SIGNED_LEN = 52;


    int
    decode(const uint8_t* buf, size_t len) noexcept;


    uint64_t expire;     // 过期时间戳
    uint32_t conv;       // 会话 ID
    uint32_t uid;        // 用户 ID
    uint32_t ip;         // 登录 IP
    uint8_t  cli_pk[32]; // 客户端 X25519 公钥
    uint8_t  sign[64];   // 登录服 Ed25519 签名


    RegistTerminalReq() = default;
    ~RegistTerminalReq() = default;
    RegistTerminalReq(const RegistTerminalReq&) = delete;
    RegistTerminalReq& operator=(const RegistTerminalReq&) = delete;
    RegistTerminalReq(RegistTerminalReq&&) = delete;
    RegistTerminalReq& operator=(RegistTerminalReq&&) = delete;
}; // struct RegistTerminalReq;


/**
 * @brief 端终注册响应, 返回的是 网关的 public key, 用于客户端生成读/写密钥
 */
struct RegistTerminalRsp {
    static constexpr int LEN = utils::X25519_KEY_LEN;


    void
    encode(uint8_t* buf, size_t len) noexcept;


    int
    decode(const uint8_t* buf, size_t len) noexcept;


    uint8_t PK[utils::X25519_KEY_LEN]; // kcp server 的 public key


    RegistTerminalRsp() = default;
    ~RegistTerminalRsp() = default;
    RegistTerminalRsp(const RegistTerminalRsp&) = delete;
    RegistTerminalRsp& operator=(const RegistTerminalRsp&) = delete;
    RegistTerminalRsp(RegistTerminalRsp&&) = delete;
    RegistTerminalRsp& operator=(RegistTerminalRsp&&) = delete;
}; // struct RegistTerminalRsp;

    
} // namespace adam::core


#endif // __ADAM_CORE_PROTO_PID_TER_REG_HPP__
