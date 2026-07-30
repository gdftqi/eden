package com

import (
	"encoding/json"
)

type ServerInfo struct {
	ID        uint32 `json:"id"`
	Protocol  string `json:"protocol"`
	Name      string `json:"name"`
	Host      string `json:"host"`
	Desc      string `json:"desc"`
	StartTime int64  `json:"start_time"`
}

func (this_ *ServerInfo) String() string {
	jstr, _ := json.Marshal(this_)
	return string(jstr)
}
