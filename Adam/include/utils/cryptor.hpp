#ifndef __ADAM_UTILS_CRYPTOR_HPP__
#define __ADAM_UTILS_CRYPTOR_HPP__


#include <cstddef>
#include <cstdint>


namespace adam::utils {


// SipHash key 长度 
constexpr size_t SIPHASH_KEY_LEN = 16;


/**
 * @brief SipHash 用于 KCP 的有效性验证
 */
uint64_t
siphash24(const void* data, size_t len, const uint8_t key[SIPHASH_KEY_LEN]) noexcept;


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


// ---- ChaCha20-Poly1305 AEAD (libsodium, IETF 96-bit nonce) ----
// 高性能(AVX2 SIMD) + 机密性&完整性一体; key 32B 正好 == SESSION_KEY_LEN,
// x25519_kx 派生的 rx/tx 可直接用满, 无需截断.
constexpr size_t XX20_KEY_LEN   = 32;
constexpr size_t XX20_NONCE_LEN = 12;
constexpr size_t XX20_TAG_LEN   = 16;


/**
 * @brief AEAD 加密 (detached): 密文与明文**等长**, 支持原地 (in == out); tag 单独输出.
 * @param tag    [out] 16B 认证标签
 * @param nonce  12B, 同一 key 下**绝不可复用** (用 conv+seq+dir 构造保证唯一)
 * @param aad    附加认证数据(只认证不加密, 如 Package 头), 可为 nullptr
 * @return 0 成功
 */
int
xx20_encrypt(const uint8_t* in, size_t inlen, uint8_t* out,
             uint8_t tag[XX20_TAG_LEN],
             const uint8_t key[XX20_KEY_LEN],
             const uint8_t nonce[XX20_NONCE_LEN],
             const uint8_t* aad = nullptr, size_t aadlen = 0) noexcept;


/**
 * @brief AEAD 解密 (detached): 验签 + 解密.验签**失败返回 -1 且不输出明文**.
 * @return 0 成功; -1 认证失败(被篡改/密钥错/nonce 错)
 */
int
xx20_decrypt(const uint8_t* in, size_t inlen, uint8_t* out,
             const uint8_t tag[XX20_TAG_LEN],
             const uint8_t key[XX20_KEY_LEN],
             const uint8_t nonce[XX20_NONCE_LEN],
             const uint8_t* aad = nullptr, size_t aadlen = 0) noexcept;


constexpr size_t ED25519_PK_LEN   = 32;   // 公钥长度
constexpr size_t ED25519_SK_LEN   = 64;   // 私钥长度 (seed 32 + pk 32)
constexpr size_t ED25519_SIGN_LEN = 64;   // 签名长度


int
ed25519_keygen(uint8_t pk[ED25519_PK_LEN], uint8_t sk[ED25519_SK_LEN]) noexcept;


int
ed25519_sign(uint8_t sig[ED25519_SIGN_LEN], const uint8_t* msg, size_t len, const uint8_t sk[ED25519_SK_LEN]) noexcept;


int
ed25519_verify(const uint8_t sig[ED25519_SIGN_LEN], const uint8_t* msg, size_t len, const uint8_t pk[ED25519_PK_LEN]) noexcept;


} // namespace adam::utils


#endif // __ADAM_UTILS_CRYPTOR_HPP__
