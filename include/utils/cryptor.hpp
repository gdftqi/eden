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


// 会话密钥长度 (crypto_kx_SESSIONKEYBYTES)
constexpr size_t SESSION_KEY_LEN = 32;


/**
 * @brief X25519 密钥协商 (客户端侧).
 * @note  内部经 BLAKE2b KDF, 并绑定双方公钥; 产出收/发两个独立方向密钥.
 *        nonce 不在此派生, 由运行时按方向各自递增管理.
 * @param rx       收方向密钥 (== 对端 tx)
 * @param tx       发方向密钥 (== 对端 rx)
 * @param self_pk  自己的公钥
 * @param self_sk  自己的私钥
 * @param peer_pk  对端 (服务端) 公钥
 * @return  成功返回 xOK
 */
int
x25519_kx_client(uint8_t rx[SESSION_KEY_LEN], uint8_t tx[SESSION_KEY_LEN],
                 const uint8_t self_pk[X25519_KEY_LEN],
                 const uint8_t self_sk[X25519_KEY_LEN],
                 const uint8_t peer_pk[X25519_KEY_LEN]) noexcept;


/**
 * @brief X25519 密钥协商 (服务端侧). 与 client 对称, rx/tx 方向相反.
 */
int
x25519_kx_server(uint8_t rx[SESSION_KEY_LEN], uint8_t tx[SESSION_KEY_LEN],
                 const uint8_t self_pk[X25519_KEY_LEN],
                 const uint8_t self_sk[X25519_KEY_LEN],
                 const uint8_t peer_pk[X25519_KEY_LEN]) noexcept;


constexpr size_t ED25519_PK_LEN   = 32;   // 公钥长度
constexpr size_t ED25519_SK_LEN   = 64;   // 私钥长度 (seed 32 + pk 32)
constexpr size_t ED25519_SIGN_LEN = 64;   // 签名长度


int
ed25519_keygen(uint8_t pk[ED25519_PK_LEN], uint8_t sk[ED25519_SK_LEN]) noexcept;


int
ed25519_sign(uint8_t sig[ED25519_SIGN_LEN], const uint8_t* msg, size_t len, const uint8_t sk[ED25519_SK_LEN]) noexcept;


int
ed25519_verify(const uint8_t sig[ED25519_SIGN_LEN], const uint8_t* msg, size_t len, const uint8_t pk[ED25519_PK_LEN]) noexcept;



} // namespace typhon::utils;


#endif // __TYPHON_UTILS_CRYPTOR_HPP__