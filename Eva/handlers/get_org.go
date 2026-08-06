package handlers

import (
	"github.com/eva/dao"
	"github.com/eva/log"
	"github.com/eva/web"
	"github.com/gin-gonic/gin"
)

const GET_ORG = "/get_org"

type getOrgReq struct {
	web.BaseRequest
}

type getOrgRsp struct {
	Departs []*dao.Department `json:"departs"`
	Users   []*dao.User       `json:"users"`
}

func GetOrg(c *gin.Context) {
	req := getOrgReq{}
	sess, err := web.BindR(c, &req)
	if err != nil {
		log.Error("GetOrg: %v", err)
		web.Response(c, -1, err.Error())
		return
	}

	departs, err := dao.GetDepartmentList()
	if err != nil {
		log.Error("GetDepartmentList failed: %v", err)
		web.Response(c, -1, "服务器内部错误, 请稍后重试")
		return
	}

	members, err := dao.GetDepartUserIDs()
	if err != nil {
		log.Error("GetDepartUserIDs failed: %v", err)
		web.Response(c, -1, "服务器内部错误, 请稍后重试")
		return
	}

	for _, d := range departs {
		d.UserIDs = members[d.ID]
	}

	users, err := dao.GetUserList()
	if err != nil {
		log.Error("GetUserList failed: %v", err)
		web.Response(c, -1, "服务器内部错误, 请稍后重试")
		return
	}

	rsp := getOrgRsp{
		Departs: departs,
		Users:   users,
	}

	web.Response(c, 0, "", sess.Tx, &rsp)
}
