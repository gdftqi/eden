package com

import (
	"encoding/binary"

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
func (this_ *Token) signedInto(buf []byte) {
	binary.LittleEndian.PutUint64(buf[offExpire:], this_.Expire)
	binary.LittleEndian.PutUint32(buf[offConv:], this_.Conv)
	binary.LittleEndian.PutUint32(buf[offUserID:], this_.UserID)
	binary.LittleEndian.PutUint32(buf[offIP:], this_.IP)
	copy(buf[offCliPK:offSign], this_.CliPK[:])
}

// 需要签名的数据
func (this_ *Token) signedBytes() []byte {
	b := make([]byte, TokenSignedLen)
	this_.signedInto(b)
	return b
}

// Marshal 序列化为 116 字节(小端 packed), 与 C++ core::Token 逐字节一致。
func (this_ *Token) Marshal() []byte {
	b := make([]byte, TokenLen)
	this_.signedInto(b)
	copy(b[offSign:], this_.Sign[:])
	return b
}

// SealedBox 非对称加密 并作签名 返回 164 字节
func (this_ *Token) SealeaBoxAndSign(ed25519Sk, x25519Pk []byte) ([]byte, error) {
	sig := utils.Ed25519Sign(ed25519Sk, this_.signedBytes())
	copy(this_.Sign[:], sig)
	return utils.SealedBoxEncrypt(this_.Marshal(), x25519Pk)
}
