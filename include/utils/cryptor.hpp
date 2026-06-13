#ifndef __TYPHON_UTILS_CRYPTOR_HPP__
#define __TYPHON_UTILS_CRYPTOR_HPP__


#include <cstddef>
#include <cstdint>


namespace typhon::utils {


/// SipHash key 长度 (字节)。固定 16B,SipHash 标准。
constexpr size_t SIPHASH_KEY_LEN = 16;

/// SipHash tag 输出长度 (字节)。SipHash-2-4 输出 64-bit = 8B。
constexpr size_t SIPHASH_TAG_LEN = 8;


/**
 * @brief 计算 SipHash-2-4 (compression rounds = 2, finalization rounds = 4)
 *
 * 标准 c=2/d=4 变体, RFC 草案与所有主流实现兼容 (Linux kernel siphash.c /
 * Aumasson reference / Python hash / Rust hashbrown 等)。
 *
 * @param data 输入字节流起始
 * @param len  输入字节数 (任意, 包括 0)
 * @param key  16 字节密钥, 调用方保证至少 SIPHASH_KEY_LEN 字节可读
 * @return     64-bit tag (host byte order, **不是网络序**;
 *             跨网络传输时调用方负责字节序处理)
 */
uint64_t
siphash24(const void* data, size_t len, const uint8_t key[SIPHASH_KEY_LEN]) noexcept;


/// AES-128 key 长度 (字节). NIST FIPS 197.
constexpr size_t AES128_KEY_LEN = 16;

/// AES block 长度 (字节). CTR 模式的 counter block / IV 也是这个长度.
constexpr size_t AES_BLOCK_LEN  = 16;


/**
 * @brief AES-128-CTR 加密.
 *
 * 以 `iv` 作为初始 counter block, 把 `data[0..len)` XOR keystream 写到
 * `out[0..len)`. 内部按 16B block 推进 counter (big-endian +1 on 整个 16B,
 * 与 NIST SP 800-38A / OpenSSL EVP_aes_128_ctr 兼容).
 *
 * @param in    明文输入起始
 * @param inlen 字节数 (任意, 包括 0). 密文长度 == inlen.
 * @param out   密文输出起始. **允许 out == in 做 in-place 加密**.
 * @param key   16 字节 AES-128 key
 * @param iv    16 字节初始 counter block (一般做法: 前 12B nonce + 后 4B 计数器,
 *              起始计数器 = 0; 调用方保证 (key, iv) 全程不复用)
 * @return  0  成功
 * @return <0  错误 (库内部失败 / 参数非法)
 */
int
aes128_ctr_encrypt(const uint8_t* in, size_t inlen, uint8_t* out,
                   const uint8_t key[AES128_KEY_LEN],
                   const uint8_t iv[AES_BLOCK_LEN]) noexcept;


/**
 * @brief AES-128-CTR 解密.
 *
 * CTR 模式 encrypt / decrypt **位等价**, 此函数本质上等同于
 * `aes128_ctr_encrypt`. 提供独立函数仅让调用方代码意图清晰.
 *
 * @see aes128_ctr_encrypt 参数与注意事项完全相同.
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





} // namespace typhon::utils;


#endif // __TYPHON_UTILS_CRYPTOR_HPP__