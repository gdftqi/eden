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
	ErrNoSession  = errors.New("会话不存在或已过期")
	ErrDecrypt    = errors.New("解密失败")
	ErrExpired    = errors.New("请求已过期")
	ErrIdentity   = errors.New("身份不一致")
)

// Timed 让 Bind 能统一校验请求时间.
//
// 时间戳必须放在密文里(而不是 HttpRequest 的外层字段): Open 的 aad 是 nil,
// 外层字段不被 AEAD 的 tag 覆盖, 放外面等于给攻击者一个可以随便改的时间戳.
type Timed interface {
	ReqTime() int64
}

// Identified 让 Bind 能校验"密文里的身份"与"外层用来找 key 的 user_id"一致.
//
// 外层 user_id 只用来定位会话密钥, 它没有被 tag 覆盖. 授权判断一律用密文里的身份.
type Identified interface {
	ReqUserID() uint32
}

// Bind 解析一次加密请求:
//
//	外层 HttpRequest{user_id, data}
//	  -> 按 user_id 取会话(rx/tx)
//	  -> 用 rx 解开 data, 反序列化进 out
//	  -> out 若实现 Timed/Identified 则一并校验
//
// 出错时已经替调用方回过响应了, handler 直接 return 即可:
//
//	sess, err := web.Bind(c, &req)
//	if err != nil {
//	    return
//	}
//	web.Response(c, 0, "", sess.Tx, rsp)
//
// 返回的 sess 带着 Tx, 用来加密响应; 带着 UserID, 那是可信身份.
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
		return nil, err
	}

	// 会话过期是正常状态, 不记 Error. 客户端收到这个应当重新登录.
	if sess == nil {
		Response(c, -1, "会话已过期, 请重新登录")
		return nil, ErrNoSession
	}

	// 解不开就说明对方没有这把会话密钥 -- 冒充别人的 user_id 到这里就断了,
	// 所以外层那个明文 user_id 不需要额外保护.
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
