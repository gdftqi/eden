package web

import (
	"errors"
	"math"
	"time"

	"github.com/eva/com"
	"github.com/eva/log"
	"github.com/gin-gonic/gin"
)

// 请求时间与服务端时间允许的偏差(秒). 超出即拒, 用来限制重放窗口.
const ReqTimeSkew = 5

var (
	ErrBadRequest = errors.New("请求格式错误")
	ErrNoSession  = errors.New("会话已过期, 请重新登录")
	ErrDecrypt    = errors.New("无效的数据")
	ErrExpired    = errors.New("请求已过期, 请重试")
	ErrIdentity   = errors.New("无效的数据")
	ErrInternal   = errors.New("服务器内部错误")
)

type Timed interface {
	ReqTime() int64
}

type Identified interface {
	ReqUserID() uint32
}

func Bind(c *gin.Context, out any) (*com.UserSession, error) {
	req := HttpRequest{}
	if err := c.BindJSON(&req); err != nil {
		log.Error("Bind: 请求格式错误: %v", err)
		return nil, ErrBadRequest
	}

	if req.UserID == 0 || len(req.Data) == 0 {
		return nil, ErrBadRequest
	}

	sess, err := com.GetUserSession(req.UserID)
	if err != nil {
		log.Error("Bind: 取会话失败: uid = %d, %v", req.UserID, err)
		return nil, ErrInternal
	}

	if sess == nil {
		return nil, ErrNoSession
	}

	if err = Decrypt(sess.Rx, req.Data, out); err != nil {
		log.Error("Bind: 解密失败: uid = %d, %v", req.UserID, err)
		return nil, ErrDecrypt
	}

	if t, ok := out.(Timed); ok {
		if math.Abs(float64(time.Now().Unix()-t.ReqTime())) > ReqTimeSkew {
			return nil, ErrExpired
		}
	}

	if u, ok := out.(Identified); ok && u.ReqUserID() != req.UserID {
		log.Error("Bind: 身份不一致: 外层 %d, 密文内 %d", req.UserID, u.ReqUserID())
		return nil, ErrIdentity
	}

	return sess, nil
}
