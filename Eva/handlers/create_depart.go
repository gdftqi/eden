package handlers

import (
	"unicode/utf8"

	"github.com/eva/dao"
	"github.com/eva/web"
	"github.com/gin-gonic/gin"
)

const CREATE_DEPART = "/create_depart"

type createDepartReq struct {
	web.BaseRequest

	Name    string  `json:"name"`
	Avatar  string  `json:"avatar,omitempty"`
	Desc    string  `json:"desc,omitempty"`
	UserIDs []int64 `json:"user_ids,omitempty"`
}

type createDepartRsp struct {
	Dept *dao.Department `json:"depart"`
}

func CreateDepart(c *gin.Context) {
	req := createDepartReq{}
	sess, err := web.BindW(c, &req)
	if err != nil {
		web.Response(c, -1, err.Error())
		return
	}

	if utf8.RuneCountInString(req.Name) == 0 || utf8.RuneCountInString(req.Name) > 16 {
		web.Response(c, -1, "部门名无效")
		return
	}

	dept := &dao.Department{
		Name:  req.Name,
		Desc:  req.Desc,
		State: 1,
	}
	err = dao.InsertDepartment(dept)
	if err != nil {
		web.Response(c, -1, err.Error())
		return
	}

	for _, userID := range req.UserIDs {
		err = dao.InsertUserDepart(userID, dept.ID)
		if err != nil {
			web.Response(c, -1, err.Error())
			return
		}
	}

	rsp := createDepartRsp{
		Dept: dept,
	}
	web.Response(c, 0, "", sess.Tx, &rsp)
}
