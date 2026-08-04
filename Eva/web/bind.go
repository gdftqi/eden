package web

import (
	"errors"
	"math"
	"time"

	"github.com/eva/com"
	"github.com/eva/log"
	"github.com/gin-gonic/gin"
)

// 请求时间与服务端时间允许的偏差(毫秒). 超出即拒.
const ReqTimeSkewMs = 5000

var (
	ErrBadRequest = errors.New("请求格式错误")
	ErrNoSession  = errors.New("会话已过期, 请重新登录")
	ErrDecrypt    = errors.New("无效的数据")
	ErrExpired    = errors.New("请求已过期, 请重试")
	ErrIdentity   = errors.New("无效的数据")
	ErrInternal   = errors.New("服务器内部错误")
	ErrDuplicate  = errors.New("请求已处理, 请勿重复提交")
)

// Timed: 请求体必须带时间戳, Bind 用它做时限校验与写请求去重.
type Timed interface {
	ReqTime() int64
}

type Identified interface {
	ReqUserID() uint32
}

type BaseRequest struct {
	Time int64 `json:"time"`
}

func (b *BaseRequest) ReqTime() int64 {
	return b.Time
}

func BindR(c *gin.Context, out any) (*com.UserSession, error) {
	return bind(c, out, false)
}

func BindW(c *gin.Context, out any) (*com.UserSession, error) {
	return bind(c, out, true)
}

func bind(c *gin.Context, out any, once bool) (*com.UserSession, error) {
	req := httpRequest{}
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

	t, timed := out.(Timed)
	if timed {
		if math.Abs(float64(time.Now().UnixMilli()-t.ReqTime())) > ReqTimeSkewMs {
			return nil, ErrExpired
		}
	}

	if u, ok := out.(Identified); ok && u.ReqUserID() != req.UserID {
		log.Error("Bind: 身份不一致: 外层 %d, 密文内 %d", req.UserID, u.ReqUserID())
		return nil, ErrIdentity
	}

	if !once {
		return sess, nil
	}

	if !timed {
		log.Fatal("BindW: 请求体没有实现 Timed, 无法去重: %T", out)
	}

	fresh, err := com.MarkRequestOnce(req.UserID, t.ReqTime())
	if err != nil {
		log.Error("Bind: 去重占位失败: uid = %d, %v", req.UserID, err)
		return nil, ErrInternal
	}

	if !fresh {
		return nil, ErrDuplicate
	}

	return sess, nil
}
