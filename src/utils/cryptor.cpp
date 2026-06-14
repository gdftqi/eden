#include "utils/cryptor.hpp"

#include <cstring>
#include <sodium.h>

#pragma GCC target("aes,sse2")
#include <wmmintrin.h>
#include <emmintrin.h>


// 64-bit 循环左移
static inline uint64_t
rotl64(uint64_t x, int b) noexcept {
    return (x << b) | (x >> (64 - b));
}


// little-endian 加载 64-bit
static inline uint64_t
load_le64(const uint8_t* p) noexcept {
    uint64_t v;
    ::memcpy(&v, p, sizeof(v));
    return v;
}


#define SIPROUND \
    do { \
        v0 += v1;  v1 = rotl64(v1, 13);  v1 ^= v0;  v0 = rotl64(v0, 32); \
        v2 += v3;  v3 = rotl64(v3, 16);  v3 ^= v2; \
        v0 += v3;  v3 = rotl64(v3, 21);  v3 ^= v0; \
        v2 += v1;  v1 = rotl64(v1, 17);  v1 ^= v2;  v2 = rotl64(v2, 32); \
    } while (0)


uint64_t
typhon::utils::siphash24(const void* data, size_t len, const uint8_t key[SIPHASH_KEY_LEN]) noexcept {
    const uint64_t k0 = load_le64(key);
    const uint64_t k1 = load_le64(key + 8);

    uint64_t v0 = k0 ^ 0x736f6d6570736575ULL;   // "somepseu"
    uint64_t v1 = k1 ^ 0x646f72616e646f6dULL;   // "dorandom"
    uint64_t v2 = k0 ^ 0x6c7967656e657261ULL;   // "lygenera"
    uint64_t v3 = k1 ^ 0x7465646279746573ULL;   // "tedbytes"

    const auto* p   = static_cast<const uint8_t*>(data);
    const auto* end = p + (len - len % 8);

    for (; p != end; p += 8) {
        const uint64_t m = load_le64(p);
        v3 ^= m;
        SIPROUND;
        SIPROUND;
        v0 ^= m;
    }

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

    v2 ^= 0xFF;
    SIPROUND;
    SIPROUND;
    SIPROUND;
    SIPROUND;

    return v0 ^ v1 ^ v2 ^ v3;
}


#undef SIPROUND


// =============================================================================
//                          X25519 (libsodium)
// =============================================================================

static_assert(typhon::utils::X25519_KEY_LEN == crypto_kx_PUBLICKEYBYTES, "X25519_PK_LEN 与 libsodium crypto_kx_PUBLICKEYBYTES 不一致");
static_assert(typhon::utils::X25519_KEY_LEN == crypto_kx_SECRETKEYBYTES, "X25519_SK_LEN 与 libsodium crypto_kx_SECRETKEYBYTES 不一致");
static_assert(typhon::utils::SESSION_KEY_LEN == crypto_kx_SESSIONKEYBYTES, "SESSION_KEY_LEN 与 libsodium crypto_kx_SESSIONKEYBYTES 不一致");


int
typhon::utils::x25519_keygen(uint8_t pk[X25519_KEY_LEN], uint8_t sk[X25519_KEY_LEN]) noexcept {
    return ::crypto_kx_keypair(pk, sk);
}


int
typhon::utils::x25519_kx_client(uint8_t rx[SESSION_KEY_LEN], uint8_t tx[SESSION_KEY_LEN],
                                const uint8_t self_pk[X25519_KEY_LEN],
                                const uint8_t self_sk[X25519_KEY_LEN],
                                const uint8_t peer_pk[X25519_KEY_LEN]) noexcept {
    // 返回 -1 表示 peer_pk 不可接受 (低阶点等), 此时不可使用派生出的密钥.
    return ::crypto_kx_client_session_keys(rx, tx, self_pk, self_sk, peer_pk);
}


int
typhon::utils::x25519_kx_server(uint8_t rx[SESSION_KEY_LEN], uint8_t tx[SESSION_KEY_LEN],
                                const uint8_t self_pk[X25519_KEY_LEN],
                                const uint8_t self_sk[X25519_KEY_LEN],
                                const uint8_t peer_pk[X25519_KEY_LEN]) noexcept {
    return ::crypto_kx_server_session_keys(rx, tx, self_pk, self_sk, peer_pk);
}


int
typhon::utils::sealedbox_encrypt(const uint8_t* in, size_t inlen, uint8_t* out, size_t* outlen,
                                const uint8_t pk[X25519_KEY_LEN]) noexcept {
    if (*outlen < inlen + crypto_box_SEALBYTES) {
        return -1;
    }

    if (::crypto_box_seal(out, in, inlen, pk) != 0) {
        return -1;
    }

    *outlen = inlen + crypto_box_SEALBYTES;
    return 0;
}


int
typhon::utils::sealedbox_decrypt(const uint8_t* in, size_t inlen, uint8_t* out, size_t* outlen,
                                const uint8_t sk[X25519_KEY_LEN], const uint8_t pk[X25519_KEY_LEN]) noexcept {
    if (inlen < crypto_box_SEALBYTES) {
        return -1;
    }

    if (*outlen < inlen - crypto_box_SEALBYTES) {
        return -1;
    }

    uint8_t derived[X25519_KEY_LEN];
    if (pk == nullptr) {
        ::crypto_scalarmult_base(derived, sk);
        pk = derived;
    }

    if (::crypto_box_seal_open(out, in, inlen, pk, sk) != 0) {
        return -1;
    }

    *outlen = inlen - crypto_box_SEALBYTES;
    return 0;
}


// =============================================================================
//                          Ed25519 签名 (libsodium)
// =============================================================================

static_assert(typhon::utils::ED25519_PK_LEN   == crypto_sign_PUBLICKEYBYTES, "ED25519_PK_LEN 与 libsodium crypto_sign_PUBLICKEYBYTES 不一致");
static_assert(typhon::utils::ED25519_SK_LEN   == crypto_sign_SECRETKEYBYTES, "ED25519_SK_LEN 与 libsodium crypto_sign_SECRETKEYBYTES 不一致");
static_assert(typhon::utils::ED25519_SIGN_LEN == crypto_sign_BYTES,          "ED25519_SIGN_LEN 与 libsodium crypto_sign_BYTES 不一致");


int
typhon::utils::ed25519_keygen(uint8_t pk[ED25519_PK_LEN], uint8_t sk[ED25519_SK_LEN]) noexcept {
    return ::crypto_sign_keypair(pk, sk);
}


int
typhon::utils::ed25519_sign(uint8_t sig[ED25519_SIGN_LEN], const uint8_t* msg, size_t len, const uint8_t sk[ED25519_SK_LEN]) noexcept {
    return ::crypto_sign_detached(sig, nullptr, msg, len, sk);
}


int
typhon::utils::ed25519_verify(const uint8_t sig[ED25519_SIGN_LEN], const uint8_t* msg, size_t len, const uint8_t pk[ED25519_PK_LEN]) noexcept {
    return ::crypto_sign_verify_detached(sig, msg, len, pk);
}


// =============================================================================
//                    ChaCha20-Poly1305 AEAD (libsodium)
// =============================================================================

static_assert(typhon::utils::XX20_KEY_LEN   == crypto_aead_chacha20poly1305_ietf_KEYBYTES,  "AEAD_KEY_LEN 与 libsodium 不一致");
static_assert(typhon::utils::XX20_NONCE_LEN == crypto_aead_chacha20poly1305_ietf_NPUBBYTES, "AEAD_NONCE_LEN 与 libsodium 不一致");
static_assert(typhon::utils::XX20_TAG_LEN   == crypto_aead_chacha20poly1305_ietf_ABYTES,    "AEAD_TAG_LEN 与 libsodium 不一致");


int
typhon::utils::xx20_encrypt(const uint8_t* in, size_t inlen, uint8_t* out,
                            uint8_t tag[XX20_TAG_LEN],
                            const uint8_t key[XX20_KEY_LEN],
                            const uint8_t nonce[XX20_NONCE_LEN],
                            const uint8_t* aad, size_t aadlen) noexcept {
    // detached: 密文写 out(可 == in), tag 写 tag; nsec 永远 nullptr
    return ::crypto_aead_chacha20poly1305_ietf_encrypt_detached(
        out, tag, nullptr, in, inlen, aad, aadlen, nullptr, nonce, key);
}


int
typhon::utils::xx20_decrypt(const uint8_t* in, size_t inlen, uint8_t* out,
                            const uint8_t tag[XX20_TAG_LEN],
                            const uint8_t key[XX20_KEY_LEN],
                            const uint8_t nonce[XX20_NONCE_LEN],
                            const uint8_t* aad, size_t aadlen) noexcept {
    // 先验 Poly1305 tag, 失败返回 -1 且不写明文; 成功才把明文写 out(可 == in)
    return ::crypto_aead_chacha20poly1305_ietf_decrypt_detached(
        out, nullptr, in, inlen, tag, aad, aadlen, nonce, key);
}