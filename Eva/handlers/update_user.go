package handlers

import (
	"github.com/eva/dao"
	"github.com/eva/web"
	"github.com/gin-gonic/gin"
)

type updateUserReq struct {
	web.BaseRequest

	Nickname     string  `json:"nickname"`
	Password     string  `json:"password"`
	PhoneNum     string  `json:"phone_num"`
	Avatar       string  `json:"avatar"`
	State        int64   `json:"state"`
	AddDepartIDs []int64 `json:"add_depart_ids"`
	DelDepartIDs []int64 `json:"del_depart_ids"`
}

type updateUserRsp struct {
	*dao.User
}

func UpdateUser(c *gin.Context) {
}
