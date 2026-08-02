package com

import (
	"encoding/json"
	"fmt"
	"time"

	"github.com/eva/mid"
)

type UserSession struct {
	UserID uint32 `json:"user_id,omitempty"`
	Conv   uint32 `json:"conv,omitempty"`
	Rx     []byte `json:"rx"`
	Tx     []byte `json:"tx"`
}

// 会话在 redis 里的存活时长. 每次成功请求会续期(见 GetUserSession).
const UserSessionTTL = time.Minute * 20

// 一个用户只有一份会话: 同账号第二处登录会覆盖前一处的 rx/tx,
// 前一处之后的请求全部解不开 -- 这是刻意的单设备约束, 与网关的顶号一致.
func userSessionKey(userID uint32) string {
	return fmt.Sprintf("USER_SESSION_%d", userID)
}

func (this_ *UserSession) String() string {
	jstr, _ := json.Marshal(this_)
	return string(jstr)
}

func (this_ *UserSession) UpdateToRedis() error {
	return mid.RedisSetEx(userSessionKey(this_.UserID), this_.String(), UserSessionTTL)
}

// GetUserSession 取会话并续期. 取不到返回 (nil, nil) --
// 会话过期是正常状态(用户很久没动), 不是错误, 调用方据此让客户端重登.
func GetUserSession(userID uint32) (*UserSession, error) {
	key := userSessionKey(userID)

	jstr, err := mid.RedisGet(key)
	if err != nil {
		return nil, err
	}

	if len(jstr) == 0 {
		return nil, nil
	}

	sess := &UserSession{}
	if err = json.Unmarshal([]byte(jstr), sess); err != nil {
		return nil, err
	}

	// 续期: 活跃用户不该因为登录满 20 分钟就被踢去重登
	if err = mid.RedisSetEx(key, jstr, UserSessionTTL); err != nil {
		return nil, err
	}

	return sess, nil
}
