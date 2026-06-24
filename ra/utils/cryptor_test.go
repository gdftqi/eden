package utils

import (
	"bytes"
	"encoding/base64"
	"encoding/binary"
	"fmt"
	"testing"
	"time"
)

// 纯 Go 内部自洽: keygen / kx 双边一致 / sealedbox / xx20 / ed25519
func TestRoundTrip(t *testing.T) {
	// ed25519 sign/verify
	edPk, edSk, err := Ed25519KeyGen()
	if err != nil {
		t.Fatal(err)
	}
	msg := []byte("hello typhon")
	sig := Ed25519Sign(edSk, msg)
	if !Ed25519Verify(edPk, msg, sig) {
		t.Fatal("ed25519 verify failed")
	}

	// sealedbox seal/open
	rpk, rsk, _ := X25519KeyGen()
	sealed, err := SealedBoxEncrypt(msg, rpk)
	if err != nil {
		t.Fatal(err)
	}
	if got, err := SealedBoxDecrypt(sealed, rpk, rsk); err != nil || !bytes.Equal(got, msg) {
		t.Fatalf("sealedbox open failed: %v", err)
	}

	// crypto_kx: 客户端 tx == 服务端 rx, 客户端 rx == 服务端 tx
	cpk, csk, _ := X25519KeyGen()
	spk, ssk, _ := X25519KeyGen()
	crx, ctx, _ := X25519KxClient(cpk, csk, spk)
	srx, stx, _ := X25519KxServer(spk, ssk, cpk)
	if !bytes.Equal(ctx, srx) || !bytes.Equal(crx, stx) {
		t.Fatal("crypto_kx 双边密钥不一致")
	}

	// xx20: 用 kx 派生的密钥加解密一条
	nonce := make([]byte, XX20NonceLen)
	ct, err := XX20Encrypt(ctx, nonce, msg, nil)
	if err != nil {
		t.Fatal(err)
	}
	if pt, err := XX20Decrypt(srx, nonce, ct, nil); err != nil || !bytes.Equal(pt, msg) {
		t.Fatalf("xx20 跨密钥解密失败: %v", err)
	}
}

// 与 libsodium(网关)逐字节兼容: 用 config 密钥签+封一个 token, 打印 base64,
// 由外部 Python/libsodium 解封+验签确认。
func TestTokenCrossCheck(t *testing.T) {
	edSk, _ := base64.StdEncoding.DecodeString("49snRJko0ayMemUHsZ5c7qj6X0Iq09np7NQBu6njl7w6O/FaWuWLST4QN43BYMwxPJdale2LNDKJ+ry2f5sFyQ==")
	gwPk, _ := base64.StdEncoding.DecodeString("33GrMPexOmZIe+q8+yUN3p+k2FkjHezAMWnpmWswu0w=")
	cliPk, _ := base64.StdEncoding.DecodeString("IeXygWC1oAuSDeZp76WiWTkAj/VvWqs+NJ043/bG2Bo=")

	signed := make([]byte, 52) // expire(8) conv(4) user_id(4) ip(4) cli_pk(32)
	binary.LittleEndian.PutUint64(signed[0:], uint64(time.Now().Unix())+10*365*86400)
	binary.LittleEndian.PutUint32(signed[8:], 2000)  // conv
	binary.LittleEndian.PutUint32(signed[12:], 90000) // user_id
	binary.LittleEndian.PutUint32(signed[16:], 0)     // ip
	copy(signed[20:], cliPk)

	token := append(signed, Ed25519Sign(edSk, signed)...) // 116B
	sealed, err := SealedBoxEncrypt(token, gwPk)          // 164B
	if err != nil {
		t.Fatal(err)
	}
	if len(sealed) != 164 {
		t.Fatalf("sealed len = %d, want 164", len(sealed))
	}
	fmt.Println("SEALED_B64=" + base64.StdEncoding.EncodeToString(sealed))
}
