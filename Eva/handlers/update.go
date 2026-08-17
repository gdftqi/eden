package handlers

import (
	"github.com/eva/log"
	"github.com/eva/web"
	"github.com/gin-gonic/gin"
)

const UPDATE = "/update"

type updateReq struct {
	web.BaseRequest
}

func Update(c *gin.Context) {
	req := updateReq{}
	sess, err := web.BindR(c, &req)
	if err != nil {
		log.Error("请求错误")
		web.Response(c, -1, "格式错误")
		return
	}

	web.Response(c, 0, "", sess.Tx, nil)
}
