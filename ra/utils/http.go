package utils

import (
	"crypto/rand"
	"encoding/base64"
	"encoding/json"
	"errors"
	"net/http"

	"github.com/gin-gonic/gin"
)

type HttpRequest struct {
	UserID uint32 `json:"user_id"`
	Data   string `json:"data"`
}

type HttpResponse struct {
	Code  int32  `json:"code"`
	Error string `json:"error,omitempty"`
	Data  any    `json:"data,omitempty"`
}

func WebResponse(c *gin.Context, code int32, err string, data ...any) {
	var d any = nil
	if len(data) > 0 {
		d = data[0]
	}

	c.JSON(http.StatusOK, HttpResponse{
		Code:  code,
		Error: err,
		Data:  d,
	})
}

// Seal 序列化 obj → 用 key(ChaCha20-Poly1305)加密 → base64( nonce(12) || 密文+tag )
// nonce 每次随机(key 是会话复用密钥, 不能用固定 nonce)
func Seal(key []byte, obj any) (string, error) {
	plain, err := json.Marshal(obj)
	if err != nil {
		return "", err
	}

	nonce := make([]byte, XX20NonceLen)
	if _, err = rand.Read(nonce); err != nil {
		return "", err
	}

	cipher, err := XX20Encrypt(key, nonce, plain, nil)
	if err != nil {
		return "", err
	}

	return base64.StdEncoding.EncodeToString(append(nonce, cipher...)), nil
}

// Open base64( nonce(12) || 密文+tag )
func Open(key []byte, b64 string, out any) error {
	raw, err := base64.StdEncoding.DecodeString(b64)
	if err != nil {
		return err
	}
	if len(raw) < XX20NonceLen {
		return errors.New("utils: ciphertext too short")
	}

	plain, err := XX20Decrypt(key, raw[:XX20NonceLen], raw[XX20NonceLen:], nil)
	if err != nil {
		return err
	}

	return json.Unmarshal(plain, out)
}
