#ifndef __ADAM_CORE_PROTO_PKID_REG_TER_HPP__
#define __ADAM_CORE_PROTO_PKID_REG_TER_HPP__


#include <cinttypes>
#include <cstddef>
#include "utils/cryptor.hpp"


namespace adam::core {


/**
 * @brief PKID_REG_TER_REQ 的 payload 数据. 对应 Eva 中的结构是 AccessToken
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
    uint32_t user_id;    // 用户 ID
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


struct RegistTerminalRsp {
    static constexpr int LEN = utils::X25519_KEY_LEN;


    uint8_t PK[utils::X25519_KEY_LEN];


    void
    encode(uint8_t* buf) noexcept;


    int
    decode(const uint8_t* buf, size_t len) noexcept;


    RegistTerminalRsp() = default;
    ~RegistTerminalRsp() = default;
    RegistTerminalRsp(const RegistTerminalRsp&) = delete;
    RegistTerminalRsp& operator=(const RegistTerminalRsp&) = delete;
    RegistTerminalRsp(RegistTerminalRsp&&) = delete;
    RegistTerminalRsp& operator=(RegistTerminalRsp&&) = delete;
}; // struct RegistTerminalRsp;

    
} // namespace adam::core


#endif // __ADAM_CORE_PROTO_PKID_REG_TER_HPP__