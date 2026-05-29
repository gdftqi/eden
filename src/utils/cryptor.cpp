#include "utils/cryptor.hpp"

#include <cstring>

// AES 部分用 AES-NI 硬件指令实现 (无外部库依赖, 无 timing side-channel).
// #pragma GCC target 让本 TU 允许生成 aes/sse2 指令, 无需全局 -maes flag;
// 不影响 siphash24 (编译器不会无故生成 AES 指令).
#pragma GCC target("aes,sse2")
#include <wmmintrin.h>   // AES-NI: _mm_aesenc_si128 等
#include <emmintrin.h>   // SSE2:   __m128i / load / store / xor


namespace {


// 64-bit 循环左移
inline uint64_t
rotl64(uint64_t x, int b) noexcept {
    return (x << b) | (x >> (64 - b));
}


// little-endian 加载 64-bit (按字节读, 避开 unaligned access UB)
inline uint64_t
load_le64(const uint8_t* p) noexcept {
    uint64_t v;
    ::memcpy(&v, p, sizeof(v));
    // x86 / ARM little-endian 上 memcpy 后直接就是 LE; 大端机会反过来,
    // 这里依赖 host LE 即可 (Linux 主流目标都是 LE).
    return v;
}


// SipRound: 论文 Figure 2.1 的核心置换
//   v0 += v1; v1 = rotl(v1,13); v1 ^= v0; v0 = rotl(v0,32);
//   v2 += v3; v3 = rotl(v3,16); v3 ^= v2;
//   v0 += v3; v3 = rotl(v3,21); v3 ^= v0;
//   v2 += v1; v1 = rotl(v1,17); v1 ^= v2; v2 = rotl(v2,32);
#define SIPROUND \
    do { \
        v0 += v1;  v1 = rotl64(v1, 13);  v1 ^= v0;  v0 = rotl64(v0, 32); \
        v2 += v3;  v3 = rotl64(v3, 16);  v3 ^= v2; \
        v0 += v3;  v3 = rotl64(v3, 21);  v3 ^= v0; \
        v2 += v1;  v1 = rotl64(v1, 17);  v1 ^= v2;  v2 = rotl64(v2, 32); \
    } while (0)


} // anonymous namespace


/**
 * SipHash-2-4 参考实现 (Aumasson & Bernstein 2012, ePrint 2012/351)
 *
 *   c = 2  压缩轮数 (每 8 字节 block 之后)
 *   d = 4  终结轮数 (整个消息处理完后)
 *
 * 与 Linux kernel siphash.c / Aumasson reference C 实现 / Rust std hash
 * 输出位等价, 可与对端无差互操作。
 */
uint64_t
typhon::utils::siphash24(const void* data, size_t len, const uint8_t key[SIPHASH_KEY_LEN]) noexcept {
    // 1. 初始化状态: 用 key 与 4 个固定常量 ("somepseudorandomlygeneratedbytes")
    const uint64_t k0 = load_le64(key);
    const uint64_t k1 = load_le64(key + 8);

    uint64_t v0 = k0 ^ 0x736f6d6570736575ULL;   // "somepseu"
    uint64_t v1 = k1 ^ 0x646f72616e646f6dULL;   // "dorandom"
    uint64_t v2 = k0 ^ 0x6c7967656e657261ULL;   // "lygenera"
    uint64_t v3 = k1 ^ 0x7465646279746573ULL;   // "tedbytes"

    // 2. 主循环: 每次吃 8 字节, c=2 轮压缩
    const auto* p   = static_cast<const uint8_t*>(data);
    const auto* end = p + (len - len % 8);

    for (; p != end; p += 8) {
        const uint64_t m = load_le64(p);
        v3 ^= m;
        SIPROUND;
        SIPROUND;
        v0 ^= m;
    }

    // 3. 处理尾部 (< 8 字节): 高位塞 (len & 0xFF) 作为长度标记, 低位塞剩余字节
    //    这一步既给消息加 "长度盐", 也对齐到 8 字节边界
    uint64_t b = static_cast<uint64_t>(len) << 56;
    const size_t tail = len & 7;
    switch (tail) {
    case 7: b |= static_cast<uint64_t>(p[6]) << 48; [[fallthrough]];
    case 6: b |= static_cast<uint64_t>(p[5]) << 40; [[fallthrough]];
    case 5: b |= static_cast<uint64_t>(p[4]) << 32; [[fallthrough]];
    case 4: b |= static_cast<uint64_t>(p[3]) << 24; [[fallthrough]];
    case 3: b |= static_cast<uint64_t>(p[2]) << 16; [[fallthrough]];
    case 2: b |= static_cast<uint64_t>(p[1]) <<  8; [[fallthrough]];
    case 1: b |= static_cast<uint64_t>(p[0]);       [[fallthrough]];
    case 0: break;
    }

    v3 ^= b;
    SIPROUND;
    SIPROUND;
    v0 ^= b;

    // 4. 终结: v2 ^= 0xFF, d=4 轮压缩, XOR 出 64-bit tag
    v2 ^= 0xFF;
    SIPROUND;
    SIPROUND;
    SIPROUND;
    SIPROUND;

    return v0 ^ v1 ^ v2 ^ v3;
}


#undef SIPROUND


// =============================================================================
//                          AES-128-CTR (AES-NI)
// =============================================================================

namespace {


// AES-128 展开后的 11 个 round key (rk[0] 是原始 key, rk[1..10] 是各轮).
struct Aes128RoundKeys {
    __m128i rk[11];
};


// key schedule 单步: 用 _mm_aeskeygenassist 的输出推下一个 round key.
// (标准 AES-NI key expansion 写法, Intel 白皮书 "AES Instruction Set" Fig.24)
static inline __m128i
aes128_expand_step(__m128i key, __m128i keygen) noexcept {
    keygen = _mm_shuffle_epi32(keygen, _MM_SHUFFLE(3, 3, 3, 3));
    key = _mm_xor_si128(key, _mm_slli_si128(key, 4));
    key = _mm_xor_si128(key, _mm_slli_si128(key, 4));
    key = _mm_xor_si128(key, _mm_slli_si128(key, 4));
    return _mm_xor_si128(key, keygen);
}


// AES-128 完整 key schedule. rcon 必须是编译期常量, 故手动展开 10 步.
static inline void
aes128_key_schedule(const uint8_t key[16], Aes128RoundKeys* ks) noexcept {
    ks->rk[0]  = _mm_loadu_si128((const __m128i*)key);
    ks->rk[1]  = aes128_expand_step(ks->rk[0],  _mm_aeskeygenassist_si128(ks->rk[0],  0x01));
    ks->rk[2]  = aes128_expand_step(ks->rk[1],  _mm_aeskeygenassist_si128(ks->rk[1],  0x02));
    ks->rk[3]  = aes128_expand_step(ks->rk[2],  _mm_aeskeygenassist_si128(ks->rk[2],  0x04));
    ks->rk[4]  = aes128_expand_step(ks->rk[3],  _mm_aeskeygenassist_si128(ks->rk[3],  0x08));
    ks->rk[5]  = aes128_expand_step(ks->rk[4],  _mm_aeskeygenassist_si128(ks->rk[4],  0x10));
    ks->rk[6]  = aes128_expand_step(ks->rk[5],  _mm_aeskeygenassist_si128(ks->rk[5],  0x20));
    ks->rk[7]  = aes128_expand_step(ks->rk[6],  _mm_aeskeygenassist_si128(ks->rk[6],  0x40));
    ks->rk[8]  = aes128_expand_step(ks->rk[7],  _mm_aeskeygenassist_si128(ks->rk[7],  0x80));
    ks->rk[9]  = aes128_expand_step(ks->rk[8],  _mm_aeskeygenassist_si128(ks->rk[8],  0x1b));
    ks->rk[10] = aes128_expand_step(ks->rk[9],  _mm_aeskeygenassist_si128(ks->rk[9],  0x36));
}


// 加密单个 128-bit block: AddRoundKey + 9×Round + FinalRound.
static inline __m128i
aes128_encrypt_block(__m128i b, const Aes128RoundKeys* ks) noexcept {
    b = _mm_xor_si128(b, ks->rk[0]);
    b = _mm_aesenc_si128(b, ks->rk[1]);
    b = _mm_aesenc_si128(b, ks->rk[2]);
    b = _mm_aesenc_si128(b, ks->rk[3]);
    b = _mm_aesenc_si128(b, ks->rk[4]);
    b = _mm_aesenc_si128(b, ks->rk[5]);
    b = _mm_aesenc_si128(b, ks->rk[6]);
    b = _mm_aesenc_si128(b, ks->rk[7]);
    b = _mm_aesenc_si128(b, ks->rk[8]);
    b = _mm_aesenc_si128(b, ks->rk[9]);
    b = _mm_aesenclast_si128(b, ks->rk[10]);
    return b;
}


// big-endian 128-bit counter +1 (NIST SP 800-38A CTR / OpenSSL EVP_aes_*_ctr 语义).
static inline void
ctr_increment(uint8_t ctr[16]) noexcept {
    for (int i = 15; i >= 0; --i) {
        if (++ctr[i] != 0) {
            break;     // 没进位, 停
        }
    }
}


} // anonymous namespace


int
typhon::utils::aes128_ctr_encrypt(const uint8_t key[AES128_KEY_LEN],
                                  const uint8_t iv[AES_BLOCK_LEN],
                                  const uint8_t* data,
                                  uint8_t* out,
                                  size_t len) noexcept {
    if (!key || !iv || (len > 0 && (!data || !out))) {
        return -1;
    }

    // 运行时确认 CPU 支持 AES-NI, 否则下面的指令会触发 SIGILL.
    // __builtin_cpu_supports 读的是 startup 期缓存的 cpuid, 开销极小.
    if (!__builtin_cpu_supports("aes")) {
        return -2;
    }

    Aes128RoundKeys ks;
    aes128_key_schedule(key, &ks);

    uint8_t ctr[AES_BLOCK_LEN];
    ::memcpy(ctr, iv, AES_BLOCK_LEN);

    size_t off = 0;
    while (off < len) {
        // keystream block = AES_Encrypt(counter)
        __m128i ctr_blk = _mm_loadu_si128((const __m128i*)ctr);
        __m128i ks_blk  = aes128_encrypt_block(ctr_blk, &ks);

        uint8_t keystream[AES_BLOCK_LEN];
        _mm_storeu_si128((__m128i*)keystream, ks_blk);

        size_t n = len - off;
        if (n > AES_BLOCK_LEN) {
            n = AES_BLOCK_LEN;
        }
        for (size_t i = 0; i < n; ++i) {
            out[off + i] = data[off + i] ^ keystream[i];
        }

        ctr_increment(ctr);
        off += n;
    }

    return 0;
}


int
typhon::utils::aes128_ctr_decrypt(const uint8_t key[AES128_KEY_LEN],
                                  const uint8_t iv[AES_BLOCK_LEN],
                                  const uint8_t* data,
                                  uint8_t* out,
                                  size_t len) noexcept {
    // CTR encrypt / decrypt 位等价.
    return aes128_ctr_encrypt(key, iv, data, out, len);
}
