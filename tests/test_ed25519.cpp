// Ed25519 keygen + sign + verify 自检
//
// 覆盖:
//   1. ed25519_keygen 成功, 且两次结果不同 (CSPRNG)
//   2. sign → verify 往返通过
//   3. 篡改消息 → 验签失败
//   4. 错误公钥 → 验签失败
//   5. 篡改签名 → 验签失败
//   6. AuthToken 实战: 对 sign 之前的字节签, 填入 sign[64], 再验; 改字段后验失败
//
// 编译运行 (从项目根目录):
//   g++ -std=c++20 -O2 -Iinclude -I/usr/local/include \
//       tests/test_ed25519.cpp src/utils/cryptor.cpp /usr/local/lib/libsodium.a \
//       -o build/test_ed25519
//   ./build/test_ed25519

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sodium.h>

#include "utils/cryptor.hpp"
#include "core/package.hpp"

using namespace typhon::utils;
using typhon::core::AuthToken;


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
    uint8_t pk[ED25519_PK_LEN], sk[ED25519_SK_LEN];
    CHECK(ed25519_keygen(pk, sk) == 0, "ed25519_keygen 成功");

    uint8_t pk2[ED25519_PK_LEN], sk2[ED25519_SK_LEN];
    ed25519_keygen(pk2, sk2);
    CHECK(std::memcmp(pk, pk2, sizeof(pk)) != 0 &&
          std::memcmp(sk, sk2, sizeof(sk)) != 0, "两次 keygen 密钥不同 (CSPRNG)");

    // 2. 签 → 验 往返
    const char*  msg  = "hello typhon —— Ed25519 sign / verify 自检 123";
    const size_t mlen = std::strlen(msg);

    uint8_t sig[ED25519_SIGN_LEN];
    CHECK(ed25519_sign(sig, (const uint8_t*)msg, mlen, sk) == 0, "签名成功");
    CHECK(ed25519_verify(sig, (const uint8_t*)msg, mlen, pk) == 0, "验签通过");

    // 3. 篡改消息 → 验失败
    {
        uint8_t bad[256];
        std::memcpy(bad, msg, mlen);
        bad[mlen / 2] ^= 0xFF;
        CHECK(ed25519_verify(sig, bad, mlen, pk) != 0, "篡改消息验签应失败");
    }

    // 4. 错误公钥 → 验失败
    CHECK(ed25519_verify(sig, (const uint8_t*)msg, mlen, pk2) != 0, "错误公钥验签应失败");

    // 5. 篡改签名 → 验失败
    sig[0] ^= 0xFF;
    CHECK(ed25519_verify(sig, (const uint8_t*)msg, mlen, pk) != 0, "篡改签名验签应失败");
    sig[0] ^= 0xFF;   // 复原

    // 6. AuthToken 实战: 对 sign 之前的字节签, 填入 sign, 再验
    {
        AuthToken tok{};
        tok.expire_ms = 1700000000000ULL;
        tok.ip        = 0x7f000001;             // 127.0.0.1
        std::memcpy(tok.cli_pk, pk2, sizeof(tok.cli_pk));

        // 签名覆盖 = sign 之前所有字段 = expire_ms(8) + ip(4) + cli_pk(32) = 44B
        const size_t signed_len = offsetof(AuthToken, sign);
        CHECK(signed_len == 8 + 4 + 32, "签名覆盖长度 == offsetof(sign) == 44");

        CHECK(ed25519_sign(tok.sign, (const uint8_t*)&tok, signed_len, sk) == 0,
              "AuthToken 签名成功");
        CHECK(ed25519_verify(tok.sign, (const uint8_t*)&tok, signed_len, pk) == 0,
              "AuthToken 验签通过");

        // 篡改 token 字段 → 验失败
        tok.ip ^= 1;
        CHECK(ed25519_verify(tok.sign, (const uint8_t*)&tok, signed_len, pk) != 0,
              "篡改 AuthToken 字段验签应失败");
    }

    if (g_fail == 0) {
        std::printf("\n全部通过 ✓\n");
    } else {
        std::printf("\n%d 项失败 ✗\n", g_fail);
    }
    return g_fail ? 1 : 0;
}
