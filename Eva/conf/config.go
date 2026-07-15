package conf

import (
	"encoding/base64"
	"errors"
	"fmt"
	"os"

	"github.com/eva/mid"
	"github.com/eva/utils"
	"gopkg.in/yaml.v3"
)

type config struct {
	ID           uint16           `yaml:"id"`
	Host         string           `yaml:"host"`
	Ed25519SkStr string           `yaml:"ed25519_sk"`  // 用于计算 Token 签名
	X25519PkStr  string           `yaml:"x25519_pk"`   // 用于对 Token 作 sealedbox 加密
	SelfSkStr    string           `yaml:"self_sk"`     // 自己用的 x25519_sk
	SelfPkStr    string           `yaml:"self_pk"`     // 自己用的 x25519_pk
	RefreshKey   string           `yaml:"refresh_key"` // refresh token key
	SipHashKey   string           `yaml:"siphash_key"` // siphash mac key
	Redis        *mid.RedisConfig `yaml:"redis"`       // redis 配置
	Etcd         *mid.EtcdConfig  `yaml:"etcd"`        // etcd 配置

	Ed25519Sk []byte `yaml:"-"` // ed25519 签名私钥, 网关会用公钥验签
	X25519Pk  []byte `yaml:"-"` // 网关的 sealedbox 加密公钥
	SelfSk    []byte `yaml:"-"` // 自己的X25519私钥
	SelfPk    []byte `yaml:"-"` // 自己的X25519公钥
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

	if tmp.ID == 0 {
		return errors.New("id is invalid")
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

	if len(tmp.SipHashKey) != 16 {
		return errors.New("siphash_key is invalid")
	}

	if len(tmp.RefreshKey) != utils.XX20KeyLen {
		return errors.New("refresh_key is invalid")
	}

	if tmp.Redis == nil {
		return errors.New("redis is invalid")
	}

	if len(tmp.Redis.Addr) == 0 {
		return errors.New("redis.addr is invalid")
	}

	if tmp.Etcd == nil {
		return errors.New("etcd is invalid")
	}

	if len(tmp.Etcd.Hosts) == 0 {
		return errors.New("etcd.hosts is invalid")
	}

	for i, host := range tmp.Etcd.Hosts {
		if len(host) == 0 {
			return fmt.Errorf("etcd.hosts[%v] is invalid", i)
		}
	}

	if len(tmp.Etcd.User) == 0 {
		return errors.New("etcd.user is invalid")
	}

	if len(tmp.Etcd.Pass) == 0 {
		return errors.New("etcd.pass is invalid")
	}

	Instance = &tmp
	return nil
}
