package handlers

import (
	"encoding/base64"
	"encoding/binary"
	"math"
	"net"
	"time"

	"github.com/gin-gonic/gin"
	"github.com/ra/com"
	"github.com/ra/conf"
	"github.com/ra/log"
	"github.com/ra/utils"
)

const USER_LOGIN = "/user_login"

type loginInfo struct {
	Username string `json:"username"`
	Password string `json:"password"`
	Time     int64  `json:"time"`
}

type userLoginReq struct {
	HPK  string `json:"hpk,omitempty"`  // http server pk
	KPK  string `json:"kpk,omitempty"`  // kcp server pk
	Info string `json:"info,omitempty"` // 加密之后的数据
}

type userLoginRsp struct {
	Conv   uint32 `json:"conv"`    // kcp conv
	UserID uint32 `json:"user_id"` // 用户ID
	Host   string `json:"host"`    // kcp host
	HostID uint32 `json:"host_id"` // kcp host id
	MacKey string `json:"mac_key"` // siphash mac key
	Token  string `json:"token"`   // 鉴权令牌
}

func UserLogin(c *gin.Context) {
	req := userLoginReq{}
	err := c.BindJSON(&req)
	if err != nil {
		utils.WebResponse(c, -1, "无效的参数")
		return
	}

	// Step 1, 检查入参
	if len(req.HPK) == 0 {
		utils.WebResponse(c, -1, "hpk is invalid")
		return
	}

	if len(req.KPK) == 0 {
		utils.WebResponse(c, -1, "kpk is invalid")
		return
	}

	if len(req.Info) == 0 {
		utils.WebResponse(c, -1, "info is invalid")
		return
	}

	hpk, err := base64.StdEncoding.DecodeString(req.HPK)
	if err != nil || len(hpk) != utils.X25519KeyLen {
		utils.WebResponse(c, -1, "hpk is invalid")
		return
	}

	kpk, err := base64.StdEncoding.DecodeString(req.KPK)
	if err != nil || len(kpk) != utils.X25519KeyLen {
		utils.WebResponse(c, -1, "kpk is invalid")
		return
	}

	// Step 2, 交换密钥
	rx, tx, err := utils.X25519KxServer(conf.Instance.SelfPk, conf.Instance.SelfSk, hpk)
	if err != nil {
		log.Error("交换密钥失败: %v", err)
		utils.WebResponse(c, -1, "服务内部错误1")
		return
	}

	// Step 3, 解密 loginInfo
	info := loginInfo{}
	if err = utils.Open(rx, req.Info, &info); err != nil {
		log.Error("解密失败: %v", err)
		utils.WebResponse(c, -1, "无效的数据")
		return
	}

	// Step 4, 检查登录信息
	if len(info.Username) == 0 || len(info.Username) > 16 {
		utils.WebResponse(c, -1, "username is invalid")
		return
	}

	if len(info.Password) != 64 {
		utils.WebResponse(c, -1, "password is invalid")
		return
	}

	if math.Abs(float64(time.Now().Unix()-info.Time)) > 10 {
		utils.WebResponse(c, -1, "user login expired")
		return
	}

	// Step 5, TODO check username & password in database
	userID := uint32(time.Now().Unix())
	conv := userID

	// Step 6, 设置会话到 redis
	sess := com.UserSession{
		UserID: userID,
		Conv:   conv,
		Tx:     tx,
		Rx:     rx,
	}

	err = sess.UpdateToRedis()
	if err != nil {
		log.Error(err)
		utils.WebResponse(c, -1, "服务器内部错误2")
		return
	}

	// Step 7, 生成令牌
	token := &com.Token{
		Expire: uint64(time.Now().Unix()) + 60, // 1 分钟有效
		Conv:   conv,
		UserID: userID,
		IP:     ipv4ToU32(c.ClientIP()),
	}
	copy(token.CliPK[:], kpk)

	sealed, err := token.SealeaBoxAndSign(conf.Instance.Ed25519Sk, conf.Instance.X25519Pk)
	if err != nil {
		log.Error("token seal 失败: %v", err)
		utils.WebResponse(c, -1, "服务器内部错误3")
		return
	}

	// Step 8, 加密应答消息
	rsp := userLoginRsp{
		Conv:   conv,
		UserID: userID,
		Host:   "13.214.204.197:5555",
		HostID: 1000,
		MacKey: conf.Instance.SipHashKey,
		Token:  base64.StdEncoding.EncodeToString(sealed),
	}

	data, err := utils.Seal(tx, rsp)
	if err != nil {
		log.Error("回包加密失败: %v", err)
		utils.WebResponse(c, -1, "服务器内部错误4")
		return
	}

	utils.WebResponse(c, 0, "", data)
}

// ipv4ToU32 把客户端 IP 字符串转成 uint32(IPv4)
func ipv4ToU32(s string) uint32 {
	ip := net.ParseIP(s)
	if ip == nil {
		return 0
	}
	if v4 := ip.To4(); v4 != nil {
		return binary.BigEndian.Uint32(v4)
	}
	return 0
}
