package com

import (
	"encoding/base64"
	"encoding/json"
	"errors"
	"fmt"

	"github.com/eva/mid"
)

const (
	// X25519 公钥长度
	x25519KeyLen = 32

	// SipHash MAC 密钥长度
	sipHashKeyLen = 16
)

// MosesInfo 网关(Moses)发布到 etcd 的完整信息
type MosesInfo struct {
	Server      ServerInfo `json:"server"`
	X25519PkStr string     `json:"x25519_pk"`
	Count       int        `json:"count"`
	KeysStr     []string   `json:"keys"`

	// ---- 以下为解码后的二进制形式, 不参与 JSON ----

	X25519Pk []byte   `json:"-"`
	Keys     [][]byte `json:"-"`
}

func (this_ *MosesInfo) String() string {
	// 不打印密钥材料, 只留可诊断的部分
	return fmt.Sprintf("{id: %08X, host: %s, keys: %d}",
		this_.Server.ID, this_.Server.Host, this_.Count)
}

// MacKeyByConv 按 conv 取该会话应使用的 SipHash 密钥
func (this_ *MosesInfo) MacKeyByConv(conv uint32) []byte {
	return this_.Keys[conv&uint32(this_.Count-1)]
}

// decode 解码并校验密钥材料
func (this_ *MosesInfo) decode() error {
	if this_.Server.ID == 0 || len(this_.Server.Host) == 0 {
		return errors.New("moses: 无效的 server 信息")
	}

	if this_.Count <= 0 || this_.Count&(this_.Count-1) != 0 {
		return fmt.Errorf("moses: count 必须是 2 的幂: %d", this_.Count)
	}

	if len(this_.KeysStr) != this_.Count {
		return fmt.Errorf("moses: keys 数量与 count 不符: %d != %d", len(this_.KeysStr), this_.Count)
	}

	pk, err := base64.StdEncoding.DecodeString(this_.X25519PkStr)
	if err != nil || len(pk) != x25519KeyLen {
		return fmt.Errorf("moses: 无效的 x25519_pk: %v", err)
	}
	this_.X25519Pk = pk

	this_.Keys = make([][]byte, 0, this_.Count)
	for i, s := range this_.KeysStr {
		k, err := base64.StdEncoding.DecodeString(s)
		if err != nil || len(k) != sipHashKeyLen {
			return fmt.Errorf("moses: 无效的 siphash key[%d]: %v", i, err)
		}

		this_.Keys = append(this_.Keys, k)
	}

	return nil
}

func makeMosesInfoFromJson(jstr []byte) (*MosesInfo, error) {
	obj := &MosesInfo{}
	if err := json.Unmarshal(jstr, obj); err != nil {
		return nil, err
	}

	if err := obj.decode(); err != nil {
		return nil, err
	}

	return obj, nil
}

// GetMosesListFromEtcd 读取网关列表
func GetMosesListFromEtcd(id ...uint32) ([]*MosesInfo, error) {
	key := "/moses"

	if len(id) > 0 {
		key = fmt.Sprintf("%s/%08X", key, id[0])
	}

	rsp, err := mid.EtcdGetPrefix(key)
	if err != nil {
		return nil, err
	}

	dataList := []*MosesInfo{}

	for _, v := range rsp.Kvs {
		mi, err := makeMosesInfoFromJson(v.Value)
		if err != nil {
			continue
		}

		dataList = append(dataList, mi)
	}

	return dataList, nil
}
