package handlers

import (
	"database/sql"
	"encoding/base64"
	"errors"
	"fmt"
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
)

const REFRESH = "/refresh"

type refreshReq struct {
	HPK          string `json:"hpk,omitempty"` // http server pk(开加密信道, 回包用)
	KPK          string `json:"kpk,omitempty"` // kcp server pk(签进新 AccessToken)
	RefreshToken string `json:"token"`
}

type refreshRsp struct {
	Conv         uint32 `json:"conv"`          // kcp conv
	UserID       uint32 `json:"user_id"`       // 用户ID
	Host         string `json:"host"`          // kcp host
	HostID       uint32 `json:"host_id"`       // kcp host id
	MacKey       string `json:"mac_key"`       // siphash mac key
	AccessToken  string `json:"access_token"`  // 网关访问Token
	RefreshToken string `json:"refresh_token"` // OAUTH Token
}

func Refresh(c *gin.Context) {
	req := refreshReq{}
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

	if len(req.RefreshToken) == 0 {
		web.Response(c, -1, "token is invalid")
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

	// Step 2, 解密 + 校验 refreshToken(失败即要求重新登录)
	refreshToken := com.RefreshToken{}
	err = refreshToken.XX20Decrypt([]byte(conf.Instance.RefreshKey), req.RefreshToken)
	if err != nil {
		web.Response(c, -1, "无效的Token")
		return
	}

	err = refreshToken.CheckFromRedis()
	if err != nil {
		log.Error("检查 RefreshToken 失败: ", err)
		web.Response(c, -1, err.Error())
		return
	}

	userID := refreshToken.UserID

	// 账号可能在发出这张 token 之后被停用了.
	user, err := dao.GetUserBasicByUserID(int64(userID))
	if err != nil {
		if errors.Is(err, sql.ErrNoRows) {
			web.Response(c, -1, "账号不存在")
			return
		}

		log.Error("GetUserBasicByUserID failed: uid = %d, %v", userID, err)
		web.Response(c, -1, "服务器内部错误, 请稍后重试")
		return
	}

	if user.State != 1 {
		// 凭据清理
		if err = com.RevokeUser(userID); err != nil {
			log.Error("RevokeUser failed: uid = %d, %v", userID, err)
		}

		web.Response(c, -1, "账号已禁用")
		return
	}

	// 单独一条限速线, 不和登录共用 --
	okID := strconv.FormatUint(uint64(userID), 10)
	if sec, err := com.RateHit("refresh", okID, conf.Instance.LoginOk); err != nil {
		log.Error("限速记账失败: %v", err)
	} else if sec > 0 {
		web.Response(c, -1, fmt.Sprintf("操作过于频繁, 请 %d 秒后再试", sec))
		return
	}

	// Step 3, 交换密钥(加密应答用)
	rx, tx, err := utils.X25519KxServer(conf.Instance.SelfPk, conf.Instance.SelfSk, hpk)
	if err != nil {
		log.Error("交换密钥失败: %v", err)
		web.Response(c, -1, "服务内部错误1")
		return
	}

	// Step 4, 按 userID 选网关(与登录同一套稳定映射), 生成新 conv。
	gwList, err := com.GetMosesListFromEtcd()
	if err != nil {
		log.Error("GetMosesListFromEtcd 失败: %v", err)
		web.Response(c, -1, "服务器内部错误1")
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
		web.Response(c, -1, "服务器内部错误2")
		return
	}

	// Step 5, 会话写入 redis
	sess := com.UserSession{
		UserID: userID,
		Conv:   conv,
		Tx:     tx,
		Rx:     rx,
	}
	if err = sess.UpdateToRedis(); err != nil {
		log.Error(err)
		web.Response(c, -1, "服务器内部错误2")
		return
	}

	// Step 6, 生成网关 AccessToken
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
		web.Response(c, -1, "服务器内部错误3")
		return
	}

	// Step 7
	uid, _ := uuid.NewUUID()
	refreshToken.Uid = uid[:]
	if err = refreshToken.UpdateToRedis(); err != nil {
		log.Error("更新 refresh token 到 redis 失败: %v", err)
		web.Response(c, -1, "服务器内部错误4")
		return
	}

	refreshData, err := refreshToken.XX20Encrypt([]byte(conf.Instance.RefreshKey))
	if err != nil {
		log.Error("refreshToken 加密失败: %v", err)
		web.Response(c, -1, "服务器内部错误5")
		return
	}

	// Step 8, 加密应答
	rsp := refreshRsp{
		Conv:         conv,
		UserID:       userID,
		Host:         gw.Server.Host,
		HostID:       gw.Server.ID,
		MacKey:       base64.StdEncoding.EncodeToString(gw.MacKeyByConv(conv)),
		AccessToken:  base64.StdEncoding.EncodeToString(sealed),
		RefreshToken: refreshData,
	}

	web.Response(c, 0, "", tx, &rsp)
}
