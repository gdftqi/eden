// Package com KCP 鉴权 token 的 Go 实现。
//
// 结构与 C++ core::Token 逐字节一致(#pragma pack(1), 小端 raw struct):
//
//	偏移  长度  字段
//	  0     8   expire    过期时间戳(秒)        uint64 LE
//	  8     4   conv      KCP conv               uint32 LE
//	 12     4   user_id   用户 ID                uint32 LE
//	 16     4   ip        登录 IP (IPv4)         uint32 LE
//	 20    32   cli_pk    客户端游戏信道 X25519 公钥
//	 ----  ---  ------------------------------------  ↑ 前 52 字节 = 签名体
//	 52    64   sign      RA Ed25519 签名(覆盖前 52 字节)
//
// 明文 116 字节;经 SealedBox 封装后 164 字节 = REGIST_REQ 的 payload。
package com

import (
	"encoding/binary"
	"errors"

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

// Token 对应 C++ core::Token。
//   - CliPK: 客户端"游戏信道"X25519 公钥(网关握手时 kx_server 用)
//   - Sign : RA 用 ed25519_sk 对前 52 字节的签名
type Token struct {
	Expire uint64
	Conv   uint32
	UserID uint32
	IP     uint32
	CliPK  [TokenCliPKLen]byte
	Sign   [TokenSignLen]byte
}

// signedInto 把签名体(前 52 字节)写进 buf[:52]
func (t *Token) signedInto(buf []byte) {
	binary.LittleEndian.PutUint64(buf[offExpire:], t.Expire)
	binary.LittleEndian.PutUint32(buf[offConv:], t.Conv)
	binary.LittleEndian.PutUint32(buf[offUserID:], t.UserID)
	binary.LittleEndian.PutUint32(buf[offIP:], t.IP)
	copy(buf[offCliPK:offSign], t.CliPK[:])
}

// SignedBytes 返回签名体(前 52 字节), 用于 Ed25519 签名 / 验签。
func (t *Token) SignedBytes() []byte {
	b := make([]byte, TokenSignedLen)
	t.signedInto(b)
	return b
}

// Marshal 序列化为 116 字节(小端 packed), 与 C++ core::Token 逐字节一致。
func (t *Token) Marshal() []byte {
	b := make([]byte, TokenLen)
	t.signedInto(b)
	copy(b[offSign:], t.Sign[:])
	return b
}

// Unmarshal 从 116 字节反序列化(不验签)。
func Unmarshal(data []byte) (*Token, error) {
	if len(data) != TokenLen {
		return nil, errors.New("com: invalid token length")
	}
	t := &Token{
		Expire: binary.LittleEndian.Uint64(data[offExpire:]),
		Conv:   binary.LittleEndian.Uint32(data[offConv:]),
		UserID: binary.LittleEndian.Uint32(data[offUserID:]),
		IP:     binary.LittleEndian.Uint32(data[offIP:]),
	}
	copy(t.CliPK[:], data[offCliPK:offSign])
	copy(t.Sign[:], data[offSign:TokenLen])
	return t, nil
}

// ---------------- 发牌 / 验牌(序列化 + 签名 + sealedbox 串起来)----------------

// SealeaBox 签发: 用 RA 的 ed25519 私钥对签名体签名(写入 t.Sign), 再用网关 x25519 公钥
// SealedBox 封装整张 token。返回 164 字节 —— 即 REGIST_REQ 的 payload。
func (t *Token) SealeaBox(raEd25519Sk, gwX25519Pk []byte) ([]byte, error) {
	sig := utils.Ed25519Sign(raEd25519Sk, t.SignedBytes())
	copy(t.Sign[:], sig)
	return utils.SealedBoxEncrypt(t.Marshal(), gwX25519Pk)
}

// Open 网关侧验牌: SealedBox 解封 → 反序列化 → ed25519 验签。
// 仅做密码学校验;expire / conv / user_id 的业务校验由调用方负责。
func Open(sealed, gwX25519Pk, gwX25519Sk, raEd25519Pk []byte) (*Token, error) {
	plain, err := utils.SealedBoxDecrypt(sealed, gwX25519Pk, gwX25519Sk)
	if err != nil {
		return nil, err
	}
	t, err := Unmarshal(plain)
	if err != nil {
		return nil, err
	}
	if !utils.Ed25519Verify(raEd25519Pk, t.SignedBytes(), t.Sign[:]) {
		return nil, errors.New("com: token signature invalid")
	}
	return t, nil
}
