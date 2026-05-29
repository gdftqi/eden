#ifndef __TYPHON_UTILS_CRYPTOR_HPP__
#define __TYPHON_UTILS_CRYPTOR_HPP__


#include <cstddef>
#include <cstdint>


namespace typhon::utils {


/**
 * @defgroup siphash SipHash-2-4 短消息 keyed MAC
 *
 * SipHash 是 Aumasson & Bernstein 2012 设计的 keyed pseudorandom function,
 * 专为**短消息 (<= 几 KB) 的 MAC / 哈希表防碰撞**优化:
 *   - 输入: 任意字节流 + 128-bit (16B) key
 *   - 输出: 64-bit (8B) tag
 *   - 安全性: 攻击者不知 key 时, 伪造一个合法 tag 平均需要 2^63 次尝试
 *   - 性能: 短消息几百 ns 级, 远快于 HMAC-SHA256, eBPF verifier 友好
 *
 * 在 typhon 里用作:
 *   - **UDP envelope MAC** (KCP frame 外面套 8B tag, XDP / userland 验证)
 *   - 后续可扩展到 hashmap 防 DoS 等场景
 *
 * @note key 必须是 16 字节,**对端必须用同一 key**。
 * @warning **不是加密**, 不提供机密性, 只提供完整性 + 来源认证。
 *          payload 本身的加密用 ChaCha20-Poly1305 (后续接入)。
 * @{
 */

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


/// @}


/**
 * @defgroup aes_ctr AES-128-CTR 流式加解密
 *
 * AES 是 NIST FIPS 197 分组密码 (128-bit block); CTR 模式把它转成流密码:
 *   keystream_i  = AES_Encrypt(key, counter_block_i)
 *   ciphertext_i = plaintext_i XOR keystream_i
 *
 * 特点:
 *   - **加密和解密完全相同操作** (都是 XOR keystream), 我们仍然提供
 *     两个函数仅为代码可读性 / 调用方意图清晰.
 *   - **流式**, 任意长度 (不要求 block 对齐), 不需要 padding,
 *     **密文长度 == 明文长度**.
 *   - **可并行 / 可随机访问**, 任意 offset 解密只要算对应 counter 即可.
 *
 * 选 128 不选 256: AES-128 是 10 轮 vs AES-256 的 14 轮, 快约 30-40%;
 * 128-bit 安全空间 (2^128) 对游戏流量已远超需求, 客户端 (尤其手机) 省 CPU.
 *
 * 在 typhon 里用作:
 *   - KCP payload 加密 (网关 ↔ 客户端方向, 配合 MAC 提供完整性)
 *
 * @warning **CTR 本身不带认证**. 攻击者翻转 ciphertext 任一 bit 就能翻转
 *          对应 plaintext bit 且无法被检测. 生产使用**必须配合 MAC**
 *          (envelope SipHash 当前只覆盖 KCP header; payload 部分需另配 MAC,
 *          或换成 AES-GCM / ChaCha20-Poly1305 这种 AEAD 模式).
 *
 * @warning **(key, iv) 组合永远不能复用**, 否则 keystream 复用泄露
 *          plaintext 异或值, 灾难性. 一般做法:
 *            - per-message 唯一 iv (随机或计数器)
 *            - 或固定 nonce 但每次换 key
 *            - 同 session 内用 counter 字段做 iv 的低位
 * @{
 */

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
 * @param key   16 字节 AES-128 key
 * @param iv    16 字节初始 counter block (一般做法: 前 12B nonce + 后 4B 计数器,
 *              起始计数器 = 0; 调用方保证 (key, iv) 全程不复用)
 * @param data  明文输入起始
 * @param out   密文输出起始. **允许 out == data 做 in-place 加密**.
 * @param len   字节数 (任意, 包括 0). 密文长度 == len.
 * @return  0  成功
 * @return <0  错误 (库内部失败 / 参数非法)
 */
int
aes128_ctr_encrypt(const uint8_t key[AES128_KEY_LEN],
                   const uint8_t iv[AES_BLOCK_LEN],
                   const uint8_t* data,
                   uint8_t* out,
                   size_t len) noexcept;


/**
 * @brief AES-128-CTR 解密.
 *
 * CTR 模式 encrypt / decrypt **位等价**, 此函数本质上等同于
 * `aes128_ctr_encrypt`. 提供独立函数仅让调用方代码意图清晰.
 *
 * @see aes128_ctr_encrypt 参数与注意事项完全相同.
 */
int
aes128_ctr_decrypt(const uint8_t key[AES128_KEY_LEN],
                   const uint8_t iv[AES_BLOCK_LEN],
                   const uint8_t* data,
                   uint8_t* out,
                   size_t len) noexcept;


/// @}


} // namespace typhon::utils;


#endif // __TYPHON_UTILS_CRYPTOR_HPP__