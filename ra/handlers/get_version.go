package handlers

import (
	"encoding/base64"

	"github.com/gin-gonic/gin"
	"github.com/ra/dao"
	"github.com/ra/log"
	"github.com/ra/utils"
)

const GET_VERSION = "/get_version"

type getVersionReq struct {
	UID string `json:"uid"`
	PK  string `json:"pk"`
}

type getVersionRsp struct {
	PK string `json:"PK"`
}

func GetVersion(c *gin.Context) {
	req := getVersionReq{}
	err := c.BindJSON(&req)
	if err != nil {
		log.Error(err)
		utils.WebResponse(c, -1, "无效的请求")
		return
	}

	// sign := c.GetHeader("X-SIGN")
	// if len(sign) != 64 {
	// 	utils.WebResponse(c, -1, "无效的签名")
	// 	return
	// }

	if len(req.UID) != 36 {
		utils.WebResponse(c, -1, "uid is invalid")
		return
	}

	peerPk, err := base64.StdEncoding.DecodeString(req.PK)
	if err != nil {
		log.Error(err)
		utils.WebResponse(c, -1, "pk is invalid")
		return
	}

	if len(peerPk) != utils.X25519KeyLen {
		utils.WebResponse(c, -1, "pk is invalid")
		return
	}

	pk, sk, err := utils.X25519KeyGen()
	if err != nil {
		log.Error(err)
		utils.WebResponse(c, -1, "服务内部错误1")
		return
	}

	rx, tx, err := utils.X25519KxServer(pk, sk, peerPk)
	if err != nil {
		log.Error(err)
		utils.WebResponse(c, -1, "服务内部错误2")
		return
	}

	sess := dao.UserSession{
		UID:   req.UID,
		RxKey: rx,
		TxKey: tx,
	}
	err = sess.UpdateToRedis()
	if err != nil {
		log.Error(err)
		utils.WebResponse(c, -1, "服务内部错误3")
		return
	}

	utils.WebResponse(c, 0, "", getVersionRsp{
		PK: base64.StdEncoding.EncodeToString(pk),
	})
}
