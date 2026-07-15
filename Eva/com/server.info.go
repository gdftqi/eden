package com

import (
	"encoding/json"
	"fmt"

	"github.com/eva/mid"
)

type ServerInfo struct {
	ID        uint32 `json:"id"`
	Protocol  string `json:"protocol"`
	Name      string `json:"name"`
	Host      string `json:"host"`
	Desc      string `json:"desc"`
	StartTime int64  `json:"start_time"`
	Nthreads  uint32 `json:"nthreads"`
}

func (this_ *ServerInfo) String() string {
	jstr, _ := json.Marshal(this_)
	return string(jstr)
}

func makeServerInfoFromJson(jstr []byte) (*ServerInfo, error) {
	obj := &ServerInfo{}
	err := json.Unmarshal(jstr, obj)
	if err != nil {
		return nil, err
	}

	return obj, nil
}

func GetServerInfoListFromEtcd(id ...uint32) ([]*ServerInfo, error) {
	key := "/cerberus"

	if len(id) > 0 {
		key = fmt.Sprintf("%s/%08x", key, id[0])
	}

	rsp, err := mid.EtcdGetPrefix(key)
	if err != nil {
		return nil, err
	}

	dataList := []*ServerInfo{}

	for _, v := range rsp.Kvs {
		si, err := makeServerInfoFromJson(v.Value)
		if err != nil {
			return nil, err
		}

		dataList = append(dataList, si)
	}

	return dataList, nil
}
