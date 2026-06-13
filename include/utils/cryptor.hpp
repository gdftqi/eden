#ifndef __TYPHON_UTILS_CRYPTOR_HPP__
#define __TYPHON_UTILS_CRYPTOR_HPP__


#include <cstddef>
#include <cstdint>


namespace typhon::utils {


// SipHash key 长度 
constexpr size_t SIPHASH_KEY_LEN = 16;

// SipHash tag 长度
constexpr size_t SIPHASH_TAG_LEN = 8;


/**
 * @brief SipHash 用于 KCP 的有效性验证
 */
uint64_t
siphash24(const void* data, size_t len, const uint8_t key[SIPHASH_KEY_LEN]) noexcept;


constexpr size_t AES128_KEY_LEN = 16;
constexpr size_t AES_BLOCK_LEN  = 16;


/**
 * @brief AES-128-CTR 加密.
 * @note  out 也可以是 in, 这样不用额外申请空间.
 * @return  成功返回 xOK
 */
int
aes128_ctr_encrypt(const uint8_t* in, size_t inlen, uint8_t* out,
                   const uint8_t key[AES128_KEY_LEN],
                   const uint8_t iv[AES_BLOCK_LEN]) noexcept;


/**
 * @brief AES-128-CTR 解密.
 */
int
aes128_ctr_decrypt(const uint8_t* in, size_t inlen, uint8_t* out,
                   const uint8_t key[AES128_KEY_LEN],
                   const uint8_t iv[AES_BLOCK_LEN]) noexcept;


constexpr size_t X25519_KEY_LEN = 32;


int
x25519_keygen(uint8_t pk[X25519_KEY_LEN], uint8_t sk[X25519_KEY_LEN]) noexcept;


int
sealedbox_encrypt(const uint8_t* in, size_t inlen, uint8_t *out, size_t* outlen, const uint8_t pk[X25519_KEY_LEN]) noexcept;


int
sealedbox_decrypt(const uint8_t* in, size_t inlen, uint8_t *out, size_t* outlen, const uint8_t sk[X25519_KEY_LEN], const uint8_t pk[X25519_KEY_LEN] = nullptr) noexcept;


constexpr size_t ED25519_PK_LEN   = 32;   // 公钥
constexpr size_t ED25519_SK_LEN   = 64;   // 私钥 (seed 32 + pk 32)
constexpr size_t ED25519_SIGN_LEN = 64;   // 签名


int
ed25519_keygen(uint8_t pk[ED25519_PK_LEN], uint8_t sk[ED25519_SK_LEN]) noexcept;


int
ed25519_sign(uint8_t sig[ED25519_SIGN_LEN], const uint8_t* msg, size_t len, const uint8_t sk[ED25519_SK_LEN]) noexcept;


int
ed25519_verify(const uint8_t sig[ED25519_SIGN_LEN], const uint8_t* msg, size_t len, const uint8_t pk[ED25519_PK_LEN]) noexcept;



} // namespace typhon::utils;


#endif // __TYPHON_UTILS_CRYPTOR_HPP__