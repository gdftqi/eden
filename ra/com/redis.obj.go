package com

import (
	"encoding/json"
	"fmt"
	"time"

	"github.com/ra/mid"
)

type UserSession struct {
	UserID uint32 `json:"user_id,omitempty"`
	Conv   uint32 `json:"conv,omitempty"`
	RxKey  []byte `json:"rx_key"`
	TxKey  []byte `json:"tx_key"`
}

func (this_ *UserSession) String() string {
	jstr, _ := json.Marshal(this_)
	return string(jstr)
}

func (this_ *UserSession) UpdateToRedis() error {
	return mid.RedisSetEx(fmt.Sprintf("USER_SESSION_%d", this_.UserID), this_.String(), time.Minute*20)
}
