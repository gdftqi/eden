package handlers

import (
	"encoding/base64"
	"time"

	"github.com/gin-gonic/gin"
	"github.com/google/uuid"
	"github.com/ra/com"
	"github.com/ra/conf"
	"github.com/ra/log"
	"github.com/ra/utils"
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

	if len(req.RefreshToken) == 0 {
		utils.WebResponse(c, -1, "token is invalid")
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

	// Step 2, 解密 + 校验 refreshToken(失败即要求重新登录)
	refreshToken := com.RefreshToken{}
	err = refreshToken.Decrypt([]byte(conf.Instance.RefreshKey), req.RefreshToken)
	if err != nil {
		utils.WebResponse(c, -1, "无效的Token")
		return
	}

	err = refreshToken.CheckFromRedis()
	if err != nil {
		log.Error("检查 RefreshToken 失败: ", err)
		utils.WebResponse(c, -1, err.Error())
		return
	}

	userID := refreshToken.UserID

	// Step 3, 交换密钥(加密应答用)
	rx, tx, err := utils.X25519KxServer(conf.Instance.SelfPk, conf.Instance.SelfSk, hpk)
	if err != nil {
		log.Error("交换密钥失败: %v", err)
		utils.WebResponse(c, -1, "服务内部错误1")
		return
	}

	// Step 4, 分配 conv(占位, 同 login; 真正的 conv 生成待 user-manager)
	conv := userID

	// Step 5, 会话写入 redis
	sess := com.UserSession{
		UserID: userID,
		Conv:   conv,
		Tx:     tx,
		Rx:     rx,
	}
	if err = sess.UpdateToRedis(); err != nil {
		log.Error(err)
		utils.WebResponse(c, -1, "服务器内部错误2")
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

	sealed, err := accessToken.SealeaBoxAndSign(conf.Instance.Ed25519Sk, conf.Instance.X25519Pk)
	if err != nil {
		log.Error("token seal 失败: %v", err)
		utils.WebResponse(c, -1, "服务器内部错误3")
		return
	}

	// Step 7
	uid, _ := uuid.NewUUID()
	refreshToken.Uid = uid[:]
	if err = refreshToken.UpdateToRedis(); err != nil {
		log.Error("更新 refresh token 到 redis 失败: %v", err)
		utils.WebResponse(c, -1, "服务器内部错误4")
		return
	}

	refreshData, err := refreshToken.Encrypt([]byte(conf.Instance.RefreshKey))
	if err != nil {
		log.Error("refreshToken 加密失败: %v", err)
		utils.WebResponse(c, -1, "服务器内部错误5")
		return
	}

	// Step 8, 加密应答
	rsp := refreshRsp{
		Conv:         conv,
		UserID:       userID,
		Host:         "172.26.29.158:5555",
		HostID:       1000,
		MacKey:       conf.Instance.SipHashKey,
		AccessToken:  base64.StdEncoding.EncodeToString(sealed),
		RefreshToken: refreshData,
	}

	data, err := utils.Seal(tx, rsp)
	if err != nil {
		log.Error("回包加密失败: %v", err)
		utils.WebResponse(c, -1, "服务器内部错误6")
		return
	}

	utils.WebResponse(c, 0, "", data)
}
