package handlers

import (
	"database/sql"
	"encoding/base64"
	"encoding/binary"
	"errors"
	"fmt"
	"math"
	"net"
	"strconv"
	"time"

	"github.com/eva/com"
	"github.com/eva/conf"
	"github.com/eva/dao"
	"github.com/eva/log"
	"github.com/eva/utils"
	"github.com/eva/web"
	"github.com/gin-gonic/gin"
	"github.com/google/uuid"
	"golang.org/x/crypto/bcrypt"
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
	Conv         uint32    `json:"conv"`          // kcp conv
	Host         string    `json:"host"`          // kcp host
	HostID       uint32    `json:"host_id"`       // kcp host id
	MacKey       string    `json:"mac_key"`       // siphash mac key
	AccessToken  string    `json:"access_token"`  // 网关访问Token
	RefreshToken string    `json:"refresh_token"` // OAUTH Token
	User         *dao.User `json:"user"`
}

func UserLogin(c *gin.Context) {
	req := userLoginReq{}
	err := c.BindJSON(&req)
	if err != nil {
		web.Response(c, -1, "无效的参数")
		return
	}

	// Step 1, 检查入参
	if len(req.HPK) == 0 {
		web.Response(c, -1, "hpk is invalid")
		return
	}

	if len(req.KPK) == 0 {
		web.Response(c, -1, "kpk is invalid")
		return
	}

	if len(req.Info) == 0 {
		web.Response(c, -1, "info is invalid")
		return
	}

	hpk, err := base64.StdEncoding.DecodeString(req.HPK)
	if err != nil || len(hpk) != utils.X25519KeyLen {
		web.Response(c, -1, "hpk is invalid")
		return
	}

	kpk, err := base64.StdEncoding.DecodeString(req.KPK)
	if err != nil || len(kpk) != utils.X25519KeyLen {
		web.Response(c, -1, "kpk is invalid")
		return
	}

	// Step 2, 交换密钥
	rx, tx, err := utils.X25519KxServer(conf.Instance.SelfPk, conf.Instance.SelfSk, hpk)
	if err != nil {
		log.Error("交换密钥失败: %v", err)
		web.Response(c, -1, "服务器内部错误1")
		return
	}

	// Step 3, 解密 loginInfo
	info := loginInfo{}
	err = web.Decrypt(rx, req.Info, &info)
	if err != nil {
		log.Error("解密失败: %v", err)
		web.Response(c, -1, "无效的数据")
		return
	}

	// Step 4, 检查登录信息
	if len(info.Username) < 5 || len(info.Username) > 16 {
		web.Response(c, -1, "username is invalid")
		return
	}

	if len(info.Password) != 64 {
		web.Response(c, -1, "password is invalid")
		return
	}

	tnow := time.Now().Unix()
	if math.Abs(float64(tnow-info.Time)) > 10 {
		web.Response(c, -1, "user login expired")
		return
	}

	// Step 5, 账号校验: 查库 + bcrypt 比对(库里是 bcrypt(客户端SHA256))
	// 用户名不存在 / 账号禁用 / 密码错误
	const errBadLogin = "用户名或密码错误"

	// 还不知道 userID 时先按 IP 限一道, 否则枚举不存在的用户名是免费的
	ipID := c.ClientIP()

	ub, err := dao.GetUserBasicByUsername(info.Username)
	if err != nil {
		if errors.Is(err, sql.ErrNoRows) {
			if _, e := com.RateHit("fail", ipID, conf.Instance.LoginFail); e != nil {
				log.Error("限速记账失败: %v", e)
			}

			web.Response(c, -1, errBadLogin)
			return
		}

		log.Error("GetUserBasicByUsername failed: %v", err)
		web.Response(c, -1, "服务器内部错误2")
		return
	}

	if ub.State != 1 {
		web.Response(c, -1, errBadLogin)
		return
	}

	userID := uint32(ub.ID)
	okID := strconv.FormatUint(uint64(userID), 10)
	failID := ipID + "|" + okID

	if sec, err := com.RateBanned(com.RateBanKey("ok", okID), com.RateBanKey("fail", failID), com.RateBanKey("fail", ipID)); err != nil {
		log.Error("限速查询失败: %v", err)
	} else if sec > 0 {
		web.Response(c, -1, fmt.Sprintf("登录过于频繁, 请 %d 秒后再试", sec))
		return
	}

	if bcrypt.CompareHashAndPassword([]byte(ub.Password), []byte(info.Password)) != nil {
		if _, err := com.RateHit("fail", failID, conf.Instance.LoginFail); err != nil {
			log.Error("限速记账失败: %v", err)
		}
		web.Response(c, -1, errBadLogin)
		return
	}

	if sec, err := com.RateHit("ok", okID, conf.Instance.LoginOk); err != nil {
		log.Error("限速记账失败: %v", err)
	} else if sec > 0 {
		web.Response(c, -1, fmt.Sprintf("登录过于频繁, 请 %d 秒后再试", sec))
		return
	}

	// Step 6, 获取网关
	gwList, err := com.GetMosesListFromEtcd()
	if err != nil {
		log.Error("GetMosesListFromEtcd 失败: %v", err)
		web.Response(c, -1, "服务器内部错误3")
		return
	}

	if len(gwList) == 0 {
		web.Response(c, -1, "无可用网关")
		return
	}

	gw := gwList[userID%uint32(len(gwList))]

	conv, err := com.GenConv(gw.Server.ID)
	if err != nil {
		log.Error("GenConv 失败: %v", err)
		web.Response(c, -1, "服务器内部错误4")
		return
	}

	macKey := gw.MacKeyByConv(conv)
	if len(macKey) == 0 {
		log.Error("网关 %08X 的 siphash 密钥无效", gw.Server.ID)
		web.Response(c, -1, "无可用网关")
		return
	}

	// Step 7, 设置会话到 redis
	sess := com.UserSession{
		UserID: userID,
		Conv:   conv,
		Tx:     tx,
		Rx:     rx,
	}

	err = sess.UpdateToRedis()
	if err != nil {
		log.Error(err)
		web.Response(c, -1, "服务器内部错误5")
		return
	}

	ub.LastLogin = tnow
	err = dao.UpdateUserBasic(ub)
	if err != nil {
		log.Error(err)
		web.Response(c, -1, "更新登录时间失败")
		return
	}

	// Step 8, 生成令牌
	accessToken := com.AccessToken{
		Expire: uint64(time.Now().Unix()) + 60, // 1 分钟有效
		Conv:   conv,
		UserID: userID,
		IP:     ipv4ToU32(c.ClientIP()),
	}
	copy(accessToken.CliPK[:], kpk)

	sealed, err := accessToken.SealeaBoxAndSign(conf.Instance.Ed25519Sk, gw.X25519Pk)
	if err != nil {
		log.Error("token seal 失败: %v", err)
		web.Response(c, -1, "服务器内部错误6")
		return
	}

	uid, _ := uuid.NewUUID()
	refreshToken := com.RefreshToken{
		UserID: userID,
		Uid:    uid[:],
	}

	err = refreshToken.UpdateToRedis()
	if err != nil {
		log.Error("更新 refresh token 到 redis 失败: %v", err)
		web.Response(c, -1, "服务器内部错误7")
		return
	}

	refreshData, err := refreshToken.XX20Encrypt([]byte(conf.Instance.RefreshKey))
	if err != nil {
		log.Error("refreshToken 加密失败: %v", err)
		web.Response(c, -1, "服务器内部错误8")
		return
	}

	user, err := dao.GetUserByID(ub.ID)
	if err != nil {
		log.Error("GetUserByID failed: %v", err)
		web.Response(c, -1, "服务端内部错误9")
		return
	}

	// Step 9, 加密应答消息
	rsp := userLoginRsp{
		Conv:         conv,
		Host:         gw.Server.Host,
		HostID:       gw.Server.ID,
		MacKey:       base64.StdEncoding.EncodeToString(macKey),
		AccessToken:  base64.StdEncoding.EncodeToString(sealed),
		RefreshToken: refreshData,
		User:         user,
	}

	web.Response(c, 0, "", tx, &rsp)
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
