package dao

import (
	"encoding/json"
	"time"

	"github.com/ra/com"
)

type UserSession struct {
	UID    string `json:"uid"`
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
	return com.RedisSetEx(this_.UID, this_.String(), time.Minute*20)
}
