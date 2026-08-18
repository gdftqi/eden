package conf

import (
	"crypto/rand"
	"encoding/base64"
	"fmt"
	"os"
	"regexp"

	"github.com/eva/log"
	"github.com/eva/utils"
)

func ensureKeys(fname string, c *config) error {
	var (
		changed bool
		notes   []string
	)

	if len(c.Ed25519SkStr) == 0 {
		pk, sk, err := utils.Ed25519KeyGen()
		if err != nil {
			return err
		}

		c.Ed25519SkStr = base64.StdEncoding.EncodeToString(sk)
		changed = true
		notes = append(notes, fmt.Sprintf(
			"ed25519 已生成, 把公钥填进网关 config.yml 的 ed25519_pk: %s",
			base64.StdEncoding.EncodeToString(pk)))
	}

	if len(c.SelfSkStr) == 0 || len(c.SelfPkStr) == 0 {
		pk, sk, err := utils.X25519KeyGen()
		if err != nil {
			return err
		}

		c.SelfPkStr = base64.StdEncoding.EncodeToString(pk)
		c.SelfSkStr = base64.StdEncoding.EncodeToString(sk)
		changed = true
		notes = append(notes, fmt.Sprintf(
			"x25519 已生成, 把公钥内置进客户端(登录信道的信任根): %s", c.SelfPkStr))
	}

	if len(c.RefreshKey) == 0 {
		buf := make([]byte, utils.XX20KeyLen)
		if _, err := rand.Read(buf); err != nil {
			return err
		}

		// refresh_key 是按原文长度校验的(不做 base64 解码), 所以要生成可打印字符
		c.RefreshKey = printable(buf)
		changed = true
		notes = append(notes, "refresh_key 已生成(重生成会让已发出的 refresh token 全部失效)")
	}

	if !changed {
		return nil
	}

	if err := rewrite(fname, map[string]string{
		"ed25519_sk":  c.Ed25519SkStr,
		"self_pk":     c.SelfPkStr,
		"self_sk":     c.SelfSkStr,
		"refresh_key": c.RefreshKey,
	}); err != nil {
		return err
	}

	for _, n := range notes {
		log.Info("%s", n)
	}
	log.Info("新密钥已写回 %s -- 这是生产密钥, 不要提交进版本库", fname)

	return nil
}

// rewrite 就地替换 yaml 顶层的 key: "value" 行, 其余内容(含注释)原样保留
func rewrite(fname string, kvs map[string]string) error {
	data, err := os.ReadFile(fname)
	if err != nil {
		return err
	}

	out := string(data)
	for k, v := range kvs {
		// 只认顶层(行首无缩进)的键, 免得误伤 redis/mysql 等嵌套块里的同名字段
		re := regexp.MustCompile(`(?m)^` + regexp.QuoteMeta(k) + `:.*$`)
		if !re.MatchString(out) {
			return fmt.Errorf("config 里找不到顶层字段 %s, 无法写回", k)
		}
		out = re.ReplaceAllLiteralString(out, fmt.Sprintf("%s: %q", k, v))
	}

	if err = os.WriteFile(fname, []byte(out), 0600); err != nil {
		return err
	}

	// WriteFile 的权限位只在创建新文件时生效, 对已存在的文件要显式 chmod.
	// 文件里是私钥, 不该让同机其他用户读到
	return os.Chmod(fname, 0600)
}

// printable 把随机字节映射成可打印字符, 长度与入参一致
func printable(buf []byte) string {
	const charset = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
	out := make([]byte, len(buf))
	for i, b := range buf {
		out[i] = charset[int(b)%len(charset)]
	}
	return string(out)
}
