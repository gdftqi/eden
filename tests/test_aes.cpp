// AES-128-CTR 自检
//
// 跑两组官方 test vector:
//   1. FIPS-197 Appendix C.1  — AES-128 单 block 加密 (间接验证 key schedule + round)
//   2. NIST SP 800-38A F.5.1  — CTR-AES128.Encrypt (验证 CTR counter 推进 + 流式)
//
// 全过 = 实现与标准位等价, 可与 OpenSSL EVP_aes_128_ctr / 任何标准实现互操作.
//
// 编译运行 (从项目根目录):
//   g++ -std=c++20 -O2 -Iinclude tests/test_aes.cpp src/utils/cryptor.cpp -o build/test_aes
//   ./build/test_aes

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "utils/cryptor.hpp"


static int g_fail = 0;


static void
hex2bin(const char* hex, uint8_t* out, size_t out_len) {
    for (size_t i = 0; i < out_len; ++i) {
        unsigned byte;
        std::sscanf(hex + i * 2, "%2x", &byte);
        out[i] = (uint8_t)byte;
    }
}


static void
check(const char* name, const uint8_t* got, const uint8_t* want, size_t len) {
    if (std::memcmp(got, want, len) == 0) {
        std::printf("[OK]   %s\n", name);
        return;
    }
    ++g_fail;
    std::printf("[FAIL] %s\n  got : ", name);
    for (size_t i = 0; i < len; ++i) std::printf("%02x", got[i]);
    std::printf("\n  want: ");
    for (size_t i = 0; i < len; ++i) std::printf("%02x", want[i]);
    std::printf("\n");
}


int
main() {
    using namespace typhon::utils;

    // --- Test 1: FIPS-197 C.1, AES-128 单 block ---
    // CTR 用 iv 作 counter, 加密一个 16B 全零明文 → 输出就是 AES_Encrypt(iv).
    // 所以令 plaintext = 0, iv = FIPS plaintext, 则 ciphertext = AES_Encrypt(key, iv).
    {
        uint8_t key[16], iv[16], zero[16] = {}, out[16], want[16];
        hex2bin("000102030405060708090a0b0c0d0e0f", key, 16);
        hex2bin("00112233445566778899aabbccddeeff", iv,  16);   // 作为 counter block
        hex2bin("69c4e0d86a7b0430d8cdb78070b4c55a", want, 16);  // FIPS-197 期望密文

        aes128_ctr_encrypt(key, iv, zero, out, 16);
        check("FIPS-197 C.1 AES-128 block", out, want, 16);
    }

    // --- Test 2: NIST SP 800-38A F.5.1 CTR-AES128.Encrypt ---
    // 4 个 block 连续加密, 验证 counter big-endian 推进.
    {
        uint8_t key[16], iv[16];
        hex2bin("2b7e151628aed2a6abf7158809cf4f3c", key, 16);
        hex2bin("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff", iv,  16);

        uint8_t pt[64], want[64], out[64];
        hex2bin("6bc1bee22e409f96e93d7e117393172a"
                "ae2d8a571e03ac9c9eb76fac45af8e51"
                "30c81c46a35ce411e5fbc1191a0a52ef"
                "f69f2445df4f9b17ad2b417be66c3710", pt, 64);
        hex2bin("874d6191b620e3261bef6864990db6ce"
                "9806f66b7970fdff8617187bb9fffdff"
                "5ae4df3edbd5d35e5b4f09020db03eab"
                "1e031dda2fbe03d1792170a0f3009cee", want, 64);

        aes128_ctr_encrypt(key, iv, pt, out, 64);
        check("NIST SP800-38A F.5.1 CTR encrypt", out, want, 64);

        // decrypt 回去应还原明文
        uint8_t back[64];
        aes128_ctr_decrypt(key, iv, out, back, 64);
        check("NIST SP800-38A F.5.1 CTR decrypt roundtrip", back, pt, 64);
    }

    // --- Test 3: 非 block 对齐长度 (流式, 不需要 padding) ---
    {
        uint8_t key[16], iv[16];
        hex2bin("2b7e151628aed2a6abf7158809cf4f3c", key, 16);
        hex2bin("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff", iv,  16);

        // 取上面 vector 的前 37 字节 (跨 2 个 block + 5 字节)
        uint8_t pt[37], want[37], out[37], back[37];
        hex2bin("6bc1bee22e409f96e93d7e117393172a"
                "ae2d8a571e03ac9c9eb76fac45af8e51"
                "30c81c46a3", pt, 37);
        hex2bin("874d6191b620e3261bef6864990db6ce"
                "9806f66b7970fdff8617187bb9fffdff"
                "5ae4df3edb", want, 37);

        aes128_ctr_encrypt(key, iv, pt, out, 37);
        check("partial-block (37B) encrypt", out, want, 37);

        aes128_ctr_decrypt(key, iv, out, back, 37);
        check("partial-block (37B) roundtrip", back, pt, 37);
    }

    // --- Test 4: in-place 加密 (out == data) ---
    {
        uint8_t key[16], iv[16], buf[64], want[64];
        hex2bin("2b7e151628aed2a6abf7158809cf4f3c", key, 16);
        hex2bin("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff", iv,  16);
        hex2bin("6bc1bee22e409f96e93d7e117393172a"
                "ae2d8a571e03ac9c9eb76fac45af8e51"
                "30c81c46a35ce411e5fbc1191a0a52ef"
                "f69f2445df4f9b17ad2b417be66c3710", buf, 64);
        hex2bin("874d6191b620e3261bef6864990db6ce"
                "9806f66b7970fdff8617187bb9fffdff"
                "5ae4df3edbd5d35e5b4f09020db03eab"
                "1e031dda2fbe03d1792170a0f3009cee", want, 64);

        aes128_ctr_encrypt(key, iv, buf, buf, 64);   // in-place
        check("in-place encrypt (out == data)", buf, want, 64);
    }

    std::printf("\n%s\n", g_fail == 0 ? "ALL PASS [OK] AES-128-CTR 与标准位等价"
                                      : "SOME FAILED [FAIL]");
    return g_fail == 0 ? 0 : 1;
}
