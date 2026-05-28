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


} // namespace typhon::utils;


#endif // __TYPHON_UTILS_CRYPTOR_HPP__