// Package utils 登录服(RA)用的密码学原语。
//
// 关键约束: 这里的实现必须与 C++ 网关(libsodium)逐字节兼容 —— 登录服签发的
// token 要能被网关验签 + 解密。下面用纯 Go x/crypto, 各算法与 libsodium 对应关系:
//
//	Ed25519*        crypto/ed25519(stdlib)        <-> crypto_sign_*           (RFC 8032)
//	SealedBox*      nacl/box.SealAnonymous        <-> crypto_box_seal/open     (匿名封装)
//	XX20*           chacha20poly1305(IETF)        <-> crypto_aead_chacha20poly1305_ietf_*
//	X25519KeyGen    nacl/box.GenerateKey          <-> crypto_kx_keypair        (X25519 keypair)
//	X25519Kx*       curve25519 + BLAKE2b-512      <-> crypto_kx_*_session_keys
package utils

import (
	"crypto/ed25519"
	"crypto/rand"
	"errors"

	"golang.org/x/crypto/blake2b"
	"golang.org/x/crypto/chacha20poly1305"
	"golang.org/x/crypto/curve25519"
	"golang.org/x/crypto/nacl/box"
)

const (
	X25519KeyLen      = 32 // X25519 公/私钥长度
	SessionKeyLen     = 32 // crypto_kx 派生的 rx/tx 长度
	Ed25519PubLen     = 32
	Ed25519SkLen      = 64 // libsodium 格式: seed(32) || pub(32)
	Ed25519SigLen     = 64
	XX20KeyLen        = 32
	XX20NonceLen      = 12 // IETF nonce
	XX20TagLen        = 16 // Poly1305 tag
	SealedBoxOverhead = 48 // crypto_box_SEALBYTES = eph_pk(32) + mac(16)
)

// X25519KeyGen 生成 X25519(crypto_kx)密钥对。
// 返回 (pk, sk) 各 32 字节。
func X25519KeyGen() (pk, sk []byte, err error) {
	pub, priv, err := box.GenerateKey(rand.Reader)
	if err != nil {
		return nil, nil, err
	}
	return pub[:], priv[:], nil
}

// kxKeys 计算 crypto_kx 的 64 字节派生: BLAKE2b-512(q || clientPk || serverPk),
// 其中 q = X25519(selfSk, peerPk)。客户端与服务端两侧的 q、hash 输入完全一致,
// 仅 rx/tx 的取法相反(见下)。
func kxKeys(selfSk, peerPk, clientPk, serverPk []byte) ([]byte, error) {
	if len(selfSk) != X25519KeyLen || len(peerPk) != X25519KeyLen ||
		len(clientPk) != X25519KeyLen || len(serverPk) != X25519KeyLen {
		return nil, errors.New("utils: invalid x25519 key length")
	}
	q, err := curve25519.X25519(selfSk, peerPk)
	if err != nil {
		return nil, err // 低阶点等不可接受的对端公钥
	}
	h, _ := blake2b.New512(nil) // 无 key, 等价 libsodium crypto_generichash(BLAKE2b-512)
	h.Write(q)
	h.Write(clientPk)
	h.Write(serverPk)
	return h.Sum(nil), nil
}

// X25519KxClient 客户端侧会话密钥派生(等价 crypto_kx_client_session_keys)。
// 返回 (rx, tx) 各 32 字节: rx = 收(== 对端 tx), tx = 发。
func X25519KxClient(clientPk, clientSk, serverPk []byte) (rx, tx []byte, err error) {
	keys, err := kxKeys(clientSk, serverPk, clientPk, serverPk)
	if err != nil {
		return nil, nil, err
	}
	return keys[0:32], keys[32:64], nil
}

// X25519KxServer 服务端侧会话密钥派生(等价 crypto_kx_server_session_keys)。
// 返回 (rx, tx) 各 32 字节。rx/tx 与客户端侧相反。
func X25519KxServer(serverPk, serverSk, clientPk []byte) (rx, tx []byte, err error) {
	keys, err := kxKeys(serverSk, clientPk, clientPk, serverPk)
	if err != nil {
		return nil, nil, err
	}
	return keys[32:64], keys[0:32], nil
}

// SealedBoxEncrypt 用接收方 X25519 公钥做匿名封装(等价 crypto_box_seal)。
// 登录服用它把 token 封给网关。返回密文 = len(msg) + SealedBoxOverhead(48) 字节。
func SealedBoxEncrypt(msg, recipientPk []byte) ([]byte, error) {
	if len(recipientPk) != X25519KeyLen {
		return nil, errors.New("utils: invalid recipient public key length")
	}
	var pk [32]byte
	copy(pk[:], recipientPk)
	return box.SealAnonymous(nil, msg, &pk, rand.Reader)
}

// SealedBoxDecrypt 用接收方密钥对解封(等价 crypto_box_seal_open)。验证失败返回 error。
func SealedBoxDecrypt(sealed, recipientPk, recipientSk []byte) ([]byte, error) {
	if len(recipientPk) != X25519KeyLen || len(recipientSk) != X25519KeyLen {
		return nil, errors.New("utils: invalid recipient key length")
	}
	var pk, sk [32]byte
	copy(pk[:], recipientPk)
	copy(sk[:], recipientSk)
	out, ok := box.OpenAnonymous(nil, sealed, &pk, &sk)
	if !ok {
		return nil, errors.New("utils: sealed box open failed")
	}
	return out, nil
}

// XX20Encrypt ChaCha20-Poly1305(IETF: 32B key, 12B nonce, 16B tag)。
// aad 可为 nil。返回 密文||tag。
func XX20Encrypt(key, nonce, plaintext, aad []byte) ([]byte, error) {
	aead, err := chacha20poly1305.New(key)
	if err != nil {
		return nil, err
	}
	if len(nonce) != aead.NonceSize() {
		return nil, errors.New("utils: invalid nonce length")
	}
	return aead.Seal(nil, nonce, plaintext, aad), nil
}

// XX20Decrypt 解密 密文||tag; 验签失败返回 error。
func XX20Decrypt(key, nonce, ciphertext, aad []byte) ([]byte, error) {
	aead, err := chacha20poly1305.New(key)
	if err != nil {
		return nil, err
	}
	if len(nonce) != aead.NonceSize() {
		return nil, errors.New("utils: invalid nonce length")
	}
	return aead.Open(nil, nonce, ciphertext, aad)
}

// Ed25519KeyGen 生成 Ed25519 密钥对。
// 返回 (pk 32B, sk 64B = seed||pub, 与 libsodium 同格式)。
func Ed25519KeyGen() (pk, sk []byte, err error) {
	pub, priv, err := ed25519.GenerateKey(rand.Reader)
	if err != nil {
		return nil, nil, err
	}
	return pub, priv, nil
}

// Ed25519Sign 用 64 字节私钥对 msg 签名(等价 crypto_sign_detached), 返回 64 字节签名。
// 登录服用它对 token 头部签名。sk 必须是 64 字节, 否则底层 ed25519.Sign 会 panic。
func Ed25519Sign(sk, msg []byte) []byte {
	return ed25519.Sign(ed25519.PrivateKey(sk), msg)
}

// Ed25519Verify 校验签名, 返回是否有效。
func Ed25519Verify(pk, msg, sig []byte) bool {
	if len(pk) != Ed25519PubLen || len(sig) != Ed25519SigLen {
		return false
	}
	return ed25519.Verify(ed25519.PublicKey(pk), msg, sig)
}
