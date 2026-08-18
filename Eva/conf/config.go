package conf

import (
	"encoding/base64"
	"errors"
	"fmt"
	"os"

	"github.com/eva/com"
	"github.com/eva/mid"
	"github.com/eva/utils"
	"gopkg.in/yaml.v3"
)

type config struct {
	ID           uint16           `yaml:"id"`
	Host         string           `yaml:"host"`
	Ed25519SkStr string           `yaml:"ed25519_sk"`  // 用于计算 Token 签名
	SelfSkStr    string           `yaml:"self_sk"`     // 自己用的 x25519_sk
	SelfPkStr    string           `yaml:"self_pk"`     // 自己用的 x25519_pk
	RefreshKey   string           `yaml:"refresh_key"` // refresh token key
	Redis        *mid.RedisConfig `yaml:"redis"`       // redis 配置
	Etcd         *mid.EtcdConfig  `yaml:"etcd"`        // etcd 配置
	Mysql        *mid.MysqlConfig `yaml:"mysql"`       // mysql 配置
	S3           *mid.S3Config    `yaml:"s3"`          // 对象存储配置
	LoginOk      *com.RateRule    `yaml:"login_ok"`
	LoginFail    *com.RateRule    `yaml:"login_fail"`

	Ed25519Sk []byte `yaml:"-"` // ed25519 签名私钥, 网关会用公钥验签
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

	// 留空的密钥字段在这里补齐并写回文件, 必须在下面的解码/校验之前
	if err = ensureKeys(fname, &tmp); err != nil {
		return err
	}

	tmp.Ed25519Sk, err = base64.StdEncoding.DecodeString(tmp.Ed25519SkStr)
	if err != nil {
		return err
	}

	if len(tmp.Ed25519Sk) != utils.Ed25519SkLen {
		return errors.New("ed25519_sk is invalid")
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

	if len(tmp.RefreshKey) != utils.XX20KeyLen {
		return errors.New("refresh_key is invalid")
	}

	// 限速配置写错(比如 max 填 0)会把所有人挡在门外, 所以宁可启动就失败, 别等线上才发现
	if err = tmp.LoginOk.Check("login_ok"); err != nil {
		return err
	}

	if err = tmp.LoginFail.Check("login_fail"); err != nil {
		return err
	}

	if tmp.S3 == nil {
		return errors.New("s3 is invalid")
	}

	if len(tmp.S3.AccessKey) == 0 {
		return errors.New("s3.access_key is invalid")
	}

	if len(tmp.S3.SecretKey) == 0 {
		return errors.New("s3.secret_key is invalid")
	}

	if len(tmp.S3.Bucket) == 0 {
		return errors.New("s3.bucket is invalid")
	}

	if len(tmp.S3.Region) == 0 {
		return errors.New("s3.region is invalid")
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

	if tmp.Mysql == nil {
		return errors.New("mysql is invalid")
	}

	if len(tmp.Mysql.Addr) == 0 {
		return errors.New("mysql.addr is invalid")
	}

	if len(tmp.Mysql.Username) == 0 {
		return errors.New("mysql.username is invalid")
	}

	if len(tmp.Mysql.Password) == 0 {
		return errors.New("mysql.password is invalid")
	}

	Instance = &tmp
	return nil
}
