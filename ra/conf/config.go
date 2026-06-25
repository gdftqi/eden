package conf

import (
	"encoding/base64"
	"errors"
	"os"

	"github.com/ra/com"
	"github.com/ra/utils"
	"gopkg.in/yaml.v3"
)

type config struct {
	Host         string           `yaml:"host"`
	Ed25519SkStr string           `yaml:"ed25519_sk"` // 用于计算 Token 签名
	X25519PkStr  string           `yaml:"x25519_pk"`  // 用于对 Token 作 sealedbox 加密
	SelfSkStr    string           `yaml:"self_sk"`    // 自己用的 x25519_sk
	SelfPkStr    string           `yaml:"self_pk"`    // 自己用的 x25519_pk
	Redis        *com.RedisConfig `yaml:"redis"`      // redis 配置
	Ed25519Sk    []byte           `yaml:"-"`
	X25519Pk     []byte           `yaml:"-"`
	SelfSk       []byte           `yaml:"-"`
	SelfPk       []byte           `yaml:"-"`
}

var Instance *config

func Init(fname string) error {
	data, err := os.ReadFile(fname)
	if err != nil {
		return err
	}

	tmp := config{}
	err = yaml.Unmarshal(data, &tmp)
	if err != nil {
		return err
	}

	if len(tmp.Host) == 0 {
		return errors.New("host is invalid")
	}

	tmp.Ed25519Sk, err = base64.StdEncoding.DecodeString(tmp.Ed25519SkStr)
	if err != nil {
		return err
	}

	if len(tmp.Ed25519Sk) != utils.Ed25519SkLen {
		return errors.New("ed25519_sk is invalid")
	}

	tmp.X25519Pk, err = base64.StdEncoding.DecodeString(tmp.X25519PkStr)
	if err != nil {
		return err
	}

	if len(tmp.X25519Pk) != utils.X25519KeyLen {
		return errors.New("x25519_pk is invalid")
	}

	tmp.SelfPk, err = base64.StdEncoding.DecodeString(tmp.SelfPkStr)
	if err != nil {
		return err
	}

	if len(tmp.SelfPk) != utils.X25519KeyLen {
		return errors.New("self_pk is invalid")
	}

	tmp.SelfSk, err = base64.StdEncoding.DecodeString(tmp.SelfSkStr)
	if err != nil {
		return err
	}

	if len(tmp.SelfSk) != utils.X25519KeyLen {
		return errors.New("self_sk is invalid")
	}

	Instance = &tmp
	return nil
}
