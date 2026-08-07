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

// 会话在 redis 里的存活时长. 每次成功请求会续期(见 GetUserSession)
const UserSessionTTL = time.Minute * 20

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

// RevokeUser 立刻踢掉一个用户
func RevokeUser(userID uint32) error {
	if err := mid.RedisDel(userSessionKey(userID)); err != nil {
		return err
	}

	return mid.RedisDel(refreshTokenKey(userID))
}

// ---------------------------- 写请求去重 ----------------------------

// 去重记录的存活时长
const ReqOnceTTL = time.Second * 15

// MarkRequestOnce 用 (用户, 毫秒时间戳) 给一次写请求占位
func MarkRequestOnce(userID uint32, ms int64) (bool, error) {
	return mid.RedisSetNx(fmt.Sprintf("REQ_ONCE_%d_%d", userID, ms), 1, ReqOnceTTL)
}

// GetUserSession 取会话并续期
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
