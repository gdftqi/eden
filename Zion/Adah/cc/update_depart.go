package cc

import (
	"database/sql"
	"errors"
	"unicode/utf8"

	"github.com/eva/log"
	"github.com/eva/web"
	"github.com/gin-gonic/gin"
	"github.com/go-sql-driver/mysql"
)

const UPDATE_DEPART = "/update_depart"

type updateDepartReq struct {
	web.BaseRequest

	DepartID   int64   `json:"depart_id"`
	Name       string  `json:"name"`
	Avatar     *string `json:"avatar,omitempty"`
	Desc       *string `json:"desc,omitempty"`
	AddUserIDs []int64 `json:"add_user_ids,omitempty"`
	DelUserIDs []int64 `json:"del_user_ids,omitempty"`
}

type updateDepartRsp struct {
	*Department
}

func UpdateDepart(c *gin.Context) {
	req := updateDepartReq{}
	sess, err := web.BindW(c, &req)
	if err != nil {
		web.Response(c, -1, err.Error())
		return
	}

	if req.DepartID <= 0 {
		web.Response(c, -1, "无效的 depart_id")
		return
	}

	depart, err := GetDepartmentByID(req.DepartID)
	if err != nil {
		log.Error("GetDepartmentByID failed: id = %d, %v", req.DepartID, err)
		if errors.Is(err, sql.ErrNoRows) {
			web.Response(c, -1, "部门不存在")
			return
		}

		web.Response(c, -1, "服务器内部错误, 请稍后重试")
		return
	}

	if len(req.Name) > 0 {
		if utf8.RuneCountInString(req.Name) > 16 {
			web.Response(c, -1, "无效的名称")
			return
		}

		depart.Name = req.Name
	}

	if req.Avatar != nil {
		depart.Avatar = *req.Avatar
	}

	if req.Desc != nil {
		if utf8.RuneCountInString(*req.Desc) > 200 {
			web.Response(c, -1, "无效的描述信息")
			return
		}

		depart.Desc = *req.Desc
	}

	if err = UpdateDepartment(depart); err != nil {
		var me *mysql.MySQLError
		if errors.As(err, &me) && me.Number == 1062 {
			web.Response(c, -1, "部门名已存在")
			return
		}

		log.Error("UpdateDepartment failed: id = %d, %v", req.DepartID, err)
		web.Response(c, -1, "服务器内部错误, 请稍后重试")
		return
	}

	if len(req.DelUserIDs) > 0 {
		if err = DeleteUsersDepart(req.DepartID, req.DelUserIDs); err != nil {
			log.Error("DeleteUsersDepart failed: id = %d, %v", req.DepartID, err)
			web.Response(c, -1, "服务器内部错误, 请稍后重试")
			return
		}
	}

	if len(req.AddUserIDs) > 0 {
		if err = UpsertUsersDepart(req.DepartID, req.AddUserIDs); err != nil {
			log.Error("UpsertUsersDepart failed: id = %d, %v", req.DepartID, err)
			web.Response(c, -1, "服务器内部错误, 请稍后重试")
			return
		}
	}

	depart, err = GetDepartmentByID(req.DepartID)
	if err != nil {
		log.Error("GetDepartmentByID failed: id = %d, %v", req.DepartID, err)
		web.Response(c, -1, "服务器内部错误, 请稍后重试")
		return
	}

	rsp := updateDepartRsp{
		Department: depart,
	}

	web.Response(c, 0, "", sess.Tx, &rsp)
}
