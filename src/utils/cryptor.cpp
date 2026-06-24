#include "utils/cryptor.hpp"

#include <cstring>
#include <sodium.h>


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