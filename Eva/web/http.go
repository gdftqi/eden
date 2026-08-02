package web

import (
	"crypto/rand"
	"encoding/base64"
	"encoding/json"
	"errors"
	"net/http"

	"github.com/eva/log"
	"github.com/eva/utils"
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

func Response(c *gin.Context, code int32, err string, data ...any) {
	var (
		d  any
		tx []byte
	)

	n := len(data)

	switch {
	case n == 0:
		d = nil

	case n == 1:
		d = data[0]

	case n == 2:
		d = data[1]
		tx = data[0].([]byte)

	default:
		log.Fatal("Response: invalid data count: %d", n)
	}

	if tx != nil {
		data, err := Encrypt(tx, d)
		if err != nil {
			log.Fatal("Seal failed: %v", err)
		}

		d = data
	}

	c.JSON(http.StatusOK, HttpResponse{
		Code:  code,
		Error: err,
		Data:  d,
	})
}

func Encrypt(tx []byte, obj any) (string, error) {
	plain, err := json.Marshal(obj)
	if err != nil {
		return "", err
	}

	nonce := make([]byte, utils.XX20NonceLen)
	if _, err = rand.Read(nonce); err != nil {
		return "", err
	}

	cipher, err := utils.XX20Encrypt(tx, nonce, plain, nil)
	if err != nil {
		return "", err
	}

	return base64.StdEncoding.EncodeToString(append(nonce, cipher...)), nil
}

// Open base64( nonce(12) || 密文+tag )
func Decrypt(key []byte, b64 string, out any) error {
	raw, err := base64.StdEncoding.DecodeString(b64)
	if err != nil {
		return err
	}
	if len(raw) < utils.XX20NonceLen {
		return errors.New("utils: ciphertext too short")
	}

	plain, err := utils.XX20Decrypt(key, raw[:utils.XX20NonceLen], raw[utils.XX20NonceLen:], nil)
	if err != nil {
		return err
	}

	return json.Unmarshal(plain, out)
}
