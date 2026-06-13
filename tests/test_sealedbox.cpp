// X25519 keygen + sealedbox (公钥加密/私钥解密) 自检
//
// 覆盖:
//   1. x25519_keygen 成功, 且两次结果不同 (CSPRNG)
//   2. sealedbox 加密 → 解密往返, 还原原文
//   3. 解密不传 pk (内部从 sk 推导) 也能还原
//   4. 错误密钥解不开
//   5. 篡改密文被 Poly1305 检测 (解密失败)
//   6. out 容量不足时返回错误, 不越界
//
// 编译运行 (从项目根目录):
//   g++ -std=c++20 -O2 -Iinclude -I/usr/local/include \
//       tests/test_sealedbox.cpp src/utils/cryptor.cpp /usr/local/lib/libsodium.a \
//       -o build/test_sealedbox
//   ./build/test_sealedbox

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sodium.h>

#include "utils/cryptor.hpp"

using namespace typhon::utils;


static int g_fail = 0;

#define CHECK(cond, msg)                                  \
    do {                                                  \
        if (cond) {                                       \
            std::printf("  [PASS] %s\n", msg);            \
        } else {                                          \
            std::printf("  [FAIL] %s\n", msg);            \
            ++g_fail;                                     \
        }                                                 \
    } while (0)


int
main() {
    if (::sodium_init() < 0) {
        std::printf("sodium_init 失败\n");
        return 1;
    }

    // 1. 生成密钥对
    uint8_t pk[KEY], sk[X25519_SK_LEN];
    CHECK(x25519_keygen(pk, sk) == 0, "x25519_keygen 成功");

    uint8_t pk2[KEY], sk2[X25519_SK_LEN];
    x25519_keygen(pk2, sk2);
    CHECK(std::memcmp(pk, pk2, sizeof(pk)) != 0 &&
          std::memcmp(sk, sk2, sizeof(sk)) != 0, "两次 keygen 密钥不同 (CSPRNG)");

    // 2. 加密 → 解密往返
    const char* msg  = "hello typhon —— X25519 sealed box 加解密自检 123";
    const size_t mlen = std::strlen(msg);

    uint8_t cipher[256];
    size_t  clen = sizeof(cipher);
    CHECK(sealedbox_encrypt((const uint8_t*)msg, mlen, cipher, &clen, pk) == 0, "encode 成功");
    CHECK(clen == mlen + crypto_box_SEALBYTES, "密文长度 == 明文 + 48");

    uint8_t plain[256];
    size_t  plen = sizeof(plain);
    CHECK(sealedbox_decrypt(cipher, clen, plain, &plen, sk, pk) == 0, "decode(带 pk) 成功");
    CHECK(plen == mlen && std::memcmp(plain, msg, mlen) == 0, "解密结果 == 原文");

    // 3. 解密不传 pk (内部从 sk 推导)
    uint8_t plain2[256];
    size_t  plen2 = sizeof(plain2);
    CHECK(sealedbox_decrypt(cipher, clen, plain2, &plen2, sk) == 0, "decode(不带 pk, 内部推导) 成功");
    CHECK(plen2 == mlen && std::memcmp(plain2, msg, mlen) == 0, "推导 pk 解密结果 == 原文");

    // 4. 错误密钥解不开
    size_t plen3 = sizeof(plain);
    CHECK(sealedbox_decrypt(cipher, clen, plain, &plen3, sk2, pk2) != 0, "错误密钥解密应失败");

    // 5. 篡改密文被检测
    cipher[clen / 2] ^= 0xFF;
    size_t plen4 = sizeof(plain);
    CHECK(sealedbox_decrypt(cipher, clen, plain, &plen4, sk, pk) != 0, "篡改密文应失败 (Poly1305)");
    cipher[clen / 2] ^= 0xFF;   // 复原

    // 6. 容量检查
    size_t too_small = mlen;    // < mlen + 48
    CHECK(sealedbox_encrypt((const uint8_t*)msg, mlen, cipher, &too_small, pk) != 0, "encode out 容量不足应失败");

    if (g_fail == 0) {
        std::printf("\n全部通过 ✓\n");
    } else {
        std::printf("\n%d 项失败 ✗\n", g_fail);
    }
    return g_fail ? 1 : 0;
}
