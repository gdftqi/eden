package handlers

import (
	"database/sql"
	"errors"

	"github.com/eva/dao"
	"github.com/eva/log"
	"github.com/eva/web"
	"github.com/gin-gonic/gin"
	"golang.org/x/crypto/bcrypt"
)

const UPDATE_USER = "/update_user"

type updateUserReq struct {
	web.BaseRequest

	Nickname     string  `json:"nickname"`
	PhoneNum     string  `json:"phone_num"`
	Avatar       string  `json:"avatar"`
	OldPassword  string  `json:"old_password"`
	Password     string  `json:"password"`
	State        int64   `json:"state"`
	AddDepartIDs []int64 `json:"add_depart_ids"`
	DelDepartIDs []int64 `json:"del_depart_ids"`
}

type updateUserRsp struct {
	*dao.User
}

func UpdateUser(c *gin.Context) {
	req := updateUserReq{}
	sess, err := web.BindW(c, &req)
	if err != nil {
		web.Response(c, -1, err.Error())
		return
	}

	var (
		ub *dao.UserBasic
		ui *dao.UserInfo
	)

	if len(req.Nickname) > 0 {
		if len(req.Nickname) > 16 {
			web.Response(c, -1, "无效的昵称")
			return
		}

		if ui, err = loadUserInfo(ui, c, req.UserID); err != nil {
			return
		}

		ui.Nickname = req.Nickname
	}

	if len(req.PhoneNum) > 0 {
		if len(req.PhoneNum) > 15 {
			web.Response(c, -1, "无效的手机号")
			return
		}

		if ui, err = loadUserInfo(ui, c, req.UserID); err != nil {
			return
		}

		ui.PhoneNum = req.PhoneNum
	}

	if len(req.Avatar) > 0 {
		if ub, err = loadUserBasic(ub, c, req.UserID); err != nil {
			return
		}

		ub.Avatar = req.Avatar
	}

	if len(req.Password) > 0 {
		if len(req.OldPassword) != 64 {
			web.Response(c, -1, "原始密码无效")
			return
		}

		if len(req.Password) != 64 {
			web.Response(c, -1, "密码无效")
			return
		}

		if ub, err = loadUserBasic(ub, c, req.UserID); err != nil {
			return
		}

		if bcrypt.CompareHashAndPassword([]byte(ub.Password), []byte(req.OldPassword)) != nil {
			web.Response(c, -1, "原始密码错误")
			return
		}

		hash, err := bcrypt.GenerateFromPassword([]byte(req.Password), bcrypt.DefaultCost)
		if err != nil {
			log.Error("bcrypt.GenerateFromPassword failed: %v", err)
			web.Response(c, -1, "服务器内部错误, 请稍后重试")
			return
		}

		ub.Password = string(hash)
	}

	// 0 表示"这次不改状态", 所以只认 1 和 -1
	if req.State != 0 {
		if req.State != 1 && req.State != -1 {
			web.Response(c, -1, "无效的状态码")
			return
		}

		if ub, err = loadUserBasic(ub, c, req.UserID); err != nil {
			return
		}

		ub.State = req.State
	}

	if ub != nil {
		if err = dao.UpdateUserBasic(ub); err != nil {
			log.Error("UpdateUserBasic failed: %v", err)
			web.Response(c, -1, "服务器内部错误, 请稍后重试")
			return
		}
	}

	if ui != nil {
		if err = dao.UpdateUserInfo(ui); err != nil {
			log.Error("UpdateUserInfo failed: %v", err)
			web.Response(c, -1, "服务器内部错误, 请稍后重试")
			return
		}
	}

	// 先删后加: 同一个部门同时出现在两个列表里时, 结果是"在里面"
	if len(req.DelDepartIDs) > 0 {
		if err = dao.DeleteUserDeparts(req.UserID, req.DelDepartIDs); err != nil {
			log.Error("DeleteUserDeparts failed: %v", err)
			web.Response(c, -1, "服务器内部错误, 请稍后重试")
			return
		}
	}

	if len(req.AddDepartIDs) > 0 {
		if err = dao.UpsertUserDeparts(req.UserID, req.AddDepartIDs); err != nil {
			log.Error("UpsertUserDeparts failed: %v", err)
			web.Response(c, -1, "服务器内部错误, 请稍后重试")
			return
		}
	}

	user, err := dao.GetUserByID(req.UserID)
	if err != nil {
		log.Error("GetUserByID failed: %v", err)
		web.Response(c, -1, "服务器内部错误, 请稍后重试")
		return
	}

	rsp := updateUserRsp{
		User: user,
	}

	web.Response(c, 0, "", sess.Tx, &rsp)
}

func loadUserInfo(ui *dao.UserInfo, c *gin.Context, userID int64) (*dao.UserInfo, error) {
	if ui != nil {
		return ui, nil
	}

	ui, err := dao.GetUserInfoByID(userID)
	if err != nil {
		log.Error("GetUserInfoByID failed: uid = %d, %v", userID, err)
		respondLoadErr(c, err)
		return nil, err
	}

	return ui, nil
}

func loadUserBasic(ub *dao.UserBasic, c *gin.Context, userID int64) (*dao.UserBasic, error) {
	if ub != nil {
		return ub, nil
	}

	ub, err := dao.GetUserBasicByUserID(userID)
	if err != nil {
		log.Error("GetUserBasicByUserID failed: uid = %d, %v", userID, err)
		respondLoadErr(c, err)
		return nil, err
	}

	return ub, nil
}

func respondLoadErr(c *gin.Context, err error) {
	if errors.Is(err, sql.ErrNoRows) {
		web.Response(c, -1, "用户不存在")
		return
	}

	web.Response(c, -1, "服务器内部错误, 请稍后重试")
}
