package handlers

import (
	"github.com/eva/dao"
	"github.com/eva/web"
	"github.com/gin-gonic/gin"
)

const UPDATE_DEPART = "/update_depart"

type updateDepartReq struct {
	web.BaseRequest

	DepartID   int64   `json:"depart_id"`
	Name       string  `json:"name"`
	Avatar     string  `json:"avatar,omitempty"`
	Desc       string  `json:"desc,omitempty"`
	AddUserIDs []int64 `json:"add_user_ids,omitempty"`
	DelUserIDs []int64 `json:"del_user_ids,omitempty"`
}

type updateDepartRsp struct {
	*dao.Department
}

// TODO: 还没实现
func UpdateDepart(c *gin.Context) {
	web.Response(c, -1, "接口尚未实现")
}
