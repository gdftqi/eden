package handlers

import (
	"errors"
	"unicode/utf8"

	"github.com/eva/dao"
	"github.com/eva/log"
	"github.com/eva/web"
	"github.com/gin-gonic/gin"
	"github.com/go-sql-driver/mysql"
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

	if len(req.Name) == 0 || utf8.RuneCountInString(req.Name) > 16 {
		web.Response(c, -1, "名称无效")
		return
	}

	// 描述是选填的(客户端那边写着"部门描述(选填)"), 只卡上限
	if utf8.RuneCountInString(req.Desc) > 200 {
		web.Response(c, -1, "描述无效")
		return
	}

	dept := &dao.Department{
		Name:   req.Name,
		Avatar: req.Avatar,
		Desc:   req.Desc,
		State:  1,
	}
	if err = dao.InsertDepartment(dept); err != nil {
		// f_name 上有 uk_name, 撞了(错误号 1062)是用户填重了, 不是服务出错.
		// 另外原来这里直接把 err.Error() 回给客户端, 会漏出 SQL 语句和表名
		var me *mysql.MySQLError
		if errors.As(err, &me) && me.Number == 1062 {
			web.Response(c, -1, "部门名已存在")
			return
		}

		log.Error("InsertDepartment failed: %v", err)
		web.Response(c, -1, "服务器内部错误, 请稍后重试")
		return
	}

	err = dao.UpsertUsersDepart(dept.ID, req.UserIDs)
	if err != nil {
		log.Error("UpsertUsersDepart failed: %v", err)
		web.Response(c, -1, "服务器内部错误, 请稍后重试")
		return
	}

	rsp := createDepartRsp{
		Dept: dept,
	}
	web.Response(c, 0, "", sess.Tx, &rsp)
}
