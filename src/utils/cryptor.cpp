#include "utils/cryptor.hpp"

#include <cstring>


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
