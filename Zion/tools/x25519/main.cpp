#include <cstdio>
#include <cstdint>
#include <sodium.h>

#include "utils/cryptor.hpp"
#include "core/error.hpp"
#include "utils/log.hpp"


int
main(int, char**) {
    ASSERT(::sodium_init() == xOK, "libsodium 初始化失败");

    uint8_t pk[adam::utils::X25519_KEY_LEN];
    uint8_t sk[adam::utils::X25519_KEY_LEN];

    if (adam::utils::x25519_keygen(pk, sk) != 0) {
        xERROR("生成密钥对失败");
        ::exit(EXIT_FAILURE);
    }

    // 32 字节二进制 → base64 字符串 (44 字符, 含 padding)
    char pk_b64[sodium_base64_ENCODED_LEN(sizeof(pk), sodium_base64_VARIANT_ORIGINAL)];
    char sk_b64[sodium_base64_ENCODED_LEN(sizeof(sk), sodium_base64_VARIANT_ORIGINAL)];
    ::sodium_bin2base64(pk_b64, sizeof(pk_b64), pk, sizeof(pk), sodium_base64_VARIANT_ORIGINAL);
    ::sodium_bin2base64(sk_b64, sizeof(sk_b64), sk, sizeof(sk), sodium_base64_VARIANT_ORIGINAL);

    xINFO("public_key  = {}", pk_b64);
    xINFO("private_key = {}", sk_b64);

    // 私钥用完即擦, 别留在内存里
    ::sodium_memzero(sk, sizeof(sk));
    ::sodium_memzero(sk_b64, sizeof(sk_b64));
    ::exit(EXIT_SUCCESS);
}
