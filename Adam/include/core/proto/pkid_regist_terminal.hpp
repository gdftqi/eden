#ifndef __ADAM_CORE_PROTO_PKID_REG_TER_HPP__
#define __ADAM_CORE_PROTO_PKID_REG_TER_HPP__


#include <cinttypes>
#include <cstddef>


namespace adam::core {


/**
 * @brief PKID_REG_TER_REQ 的 payload 数据
 */
struct AccessToken {
    /**
     * @brief 明文总长(线上; 与含对齐 padding 的 sizeof(AccessToken) 无关)
     */
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


    AccessToken() = default;
    ~AccessToken() = default;
    AccessToken(const AccessToken&) = delete;
    AccessToken& operator=(const AccessToken&) = delete;
    AccessToken(AccessToken&&) = delete;
    AccessToken& operator=(AccessToken&&) = delete;
}; // struct AccessToken;

    
} // namespace adam::core


#endif // __ADAM_CORE_PROTO_PKID_REG_TER_HPP__