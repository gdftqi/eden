package handlers

import (
	"github.com/eva/dao"
	"github.com/eva/web"
	"github.com/gin-gonic/gin"
)

type updateDepartReq struct {
	web.BaseRequest

	Name       string  `json:"name"`
	Avatar     string  `json:"avatar,omitempty"`
	Desc       string  `json:"desc,omitempty"`
	AddUserIDs []int64 `json:"add_user_ids,omitempty"`
	DelUserIDs []int64 `json:"del_user_ids,omitempty"`
}

type updateDepartRsp struct {
	*dao.Department
}

func UpdateDepart(c *gin.Context) {
}
