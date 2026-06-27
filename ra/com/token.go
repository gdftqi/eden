package com

import (
	"bytes"
	"crypto/rand"
	"encoding/base64"
	"encoding/binary"
	"encoding/json"
	"errors"
	"fmt"
	"time"

	"github.com/ra/mid"
	"github.com/ra/utils"
)

const (
	TokenCliPKLen  = 32
	TokenSignLen   = 64
	TokenSignedLen = 8 + 4 + 4 + 4 + TokenCliPKLen      // 52: 被签名覆盖的部分 (== C++ offsetof(Token, sign))
	TokenLen       = TokenSignedLen + TokenSignLen      // 116: 明文 token 总长 (== C++ sizeof(Token))
	TokenSealedLen = TokenLen + utils.SealedBoxOverhead // 164: 封装后 (== REGIST_REQ 的 payload 长度)
)

// 各字段在序列化缓冲里的偏移。
const (
	offExpire = 0
	offConv   = 8
	offUserID = 12
	offIP     = 16
	offCliPK  = 20
	offSign   = TokenSignedLen // 52
)

// AccessToken 对应 C++ core::AccessToken。
//   - CliPK: 客户端"游戏信道"X25519 公钥(网关握手时 kx_server 用)
//   - Sign : RA 用 ed25519_sk 对前 52 字节的签名
type AccessToken struct {
	Expire uint64
	Conv   uint32
	UserID uint32
	IP     uint32
	CliPK  [TokenCliPKLen]byte
	Sign   [TokenSignLen]byte
}

// signedInto 把签名体(前 52 字节)写进 buf[:52]
func (this_ *AccessToken) signedInto(buf []byte) {
	binary.LittleEndian.PutUint64(buf[offExpire:], this_.Expire)
	binary.LittleEndian.PutUint32(buf[offConv:], this_.Conv)
	binary.LittleEndian.PutUint32(buf[offUserID:], this_.UserID)
	binary.LittleEndian.PutUint32(buf[offIP:], this_.IP)
	copy(buf[offCliPK:offSign], this_.CliPK[:])
}

// 需要签名的数据
func (this_ *AccessToken) signedBytes() []byte {
	b := make([]byte, TokenSignedLen)
	this_.signedInto(b)
	return b
}

// Marshal 序列化为 116 字节(小端 packed), 与 C++ core::Token 逐字节一致。
func (this_ *AccessToken) Marshal() []byte {
	b := make([]byte, TokenLen)
	this_.signedInto(b)
	copy(b[offSign:], this_.Sign[:])
	return b
}

// SealedBox 非对称加密 并作签名 返回 164 字节
func (this_ *AccessToken) SealeaBoxAndSign(ed25519Sk, x25519Pk []byte) ([]byte, error) {
	sig := utils.Ed25519Sign(ed25519Sk, this_.signedBytes())
	copy(this_.Sign[:], sig)
	return utils.SealedBoxEncrypt(this_.Marshal(), x25519Pk)
}

type RefreshToken struct {
	UserID uint32 `json:"user_id"`
	Uid    []byte `json:"uid"`
}

func (this_ *RefreshToken) String() string {
	return utils.ToJSON(this_)
}

// Encrypt 序列化 + 用 key(RA 专用对称密钥)ChaCha20-Poly1305 加密。
// 返回 nonce(12) || 密文+tag; 调用方再 base64 放进应答。每次随机 nonce, 不复用。
func (this_ *RefreshToken) Encrypt(key []byte) (string, error) {
	plain, err := json.Marshal(this_)
	if err != nil {
		return "", err
	}

	nonce := make([]byte, utils.XX20NonceLen)
	if _, err = rand.Read(nonce); err != nil {
		return "", err
	}

	cipher, err := utils.XX20Encrypt(key, nonce, plain, nil)
	if err != nil {
		return "", err
	}

	data := append(nonce, cipher...)
	return base64.StdEncoding.EncodeToString(data), nil
}

// Decrypt 用同一把 key 解 nonce(12) || 密文+tag, 反序列化回 this_。
// tag 校验失败(被篡改 / 错误 key)会返回 error。
func (this_ *RefreshToken) Decrypt(key []byte, b64Data string) error {
	data, err := base64.StdEncoding.DecodeString(b64Data)
	if err != nil {
		return err
	}

	if len(data) < utils.XX20NonceLen {
		return errors.New("com: refresh token too short")
	}

	plain, err := utils.XX20Decrypt(key, data[:utils.XX20NonceLen], data[utils.XX20NonceLen:], nil)
	if err != nil {
		return err
	}

	return json.Unmarshal(plain, this_)
}

func (this_ *RefreshToken) UpdateToRedis() error {
	return mid.RedisSetEx(fmt.Sprintf("REFRESH_TOKEN_%d", this_.UserID), this_.Uid, time.Hour*24*30)
}

func (this_ *RefreshToken) CheckFromRedis() error {
	uid, err := mid.RedisGet(fmt.Sprintf("REFRESH_TOKEN_%d", this_.UserID))
	if err != nil {
		return err
	}

	data := []byte(uid)

	// uid 与 redis 里存的不一致 = 已被改密/登出/顶号换掉 → 失效
	if !bytes.Equal(data, this_.Uid) {
		return errors.New("无效的Token")
	}

	return nil
}
