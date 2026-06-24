package handlers

import (
	"github.com/gin-gonic/gin"
	"github.com/ra/log"
	"github.com/ra/utils"
)

const GET_VERSION = "/ger_version"

type getVersionReq struct {
	UID string `json:"uid"`
	PK  string `json:"pk"`
}

type getVersionRsp struct {
}

func GetVersion(c *gin.Context) {
	req := getVersionReq{}
	err := c.BindJSON(req)
	if err != nil {
		log.Error(err)
		utils.WebResponse(c, -1, "无效的请求")
		return
	}

	sign := c.GetHeader("X-SIGN")
	if len(sign) != 64 {
		utils.WebResponse(c, -1, "无效的签名")
		return
	}

	if len(req.UID) != 36 {
		utils.WebResponse(c, -1, "uid is invalid")
		return
	}

	if len(req.PK) != utils.X25519KeyLen {
		utils.WebResponse(c, -1, "pk is invalid")
		return
	}
}
