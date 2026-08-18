package handlers

import (
	"database/sql"
	"errors"

	"github.com/eva/com"
	"github.com/eva/dao"
	"github.com/eva/log"
	"github.com/eva/web"
	"github.com/gin-gonic/gin"
	"golang.org/x/crypto/bcrypt"
)

const UPDATE_USER = "/update_user"

type updateUserReq struct {
	web.BaseRequest

	// 指针: nil = 这次不改, "" = 改成空.
	// 用 string 的话这两种情况都是空串, 头像就只能改不能清
	Avatar *string `json:"avatar,omitempty"`

	OldPassword string `json:"old_password"`
	Password    string `json:"password"`
	State       int64  `json:"state"`
}

type updateUserRsp struct {
	User any `json:"user"`
}

func UpdateUser(c *gin.Context) {
	req := updateUserReq{}
	sess, err := web.BindW(c, &req)
	if err != nil {
		web.Response(c, -1, err.Error())
		return
	}

	var ub *dao.UserBasic

	if req.Avatar != nil {
		if ub, err = loadUserBasic(ub, c, req.UserID); err != nil {
			return
		}

		ub.Avatar = *req.Avatar
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

	// 改密码或停用之后把对方踢下线.
	revoke := len(req.Password) > 0 || req.State == -1

	if ub != nil {
		if err = dao.UpdateUserBasic(ub); err != nil {
			log.Error("UpdateUserBasic failed: %v", err)
			web.Response(c, -1, "服务器内部错误, 请稍后重试")
			return
		}

		if revoke {
			if err = com.RevokeUser(uint32(req.UserID)); err != nil {
				log.Error("RevokeUser failed: uid = %d, %v", req.UserID, err)
				web.Response(c, -1, "服务器内部错误, 请稍后重试")
				return
			}
		}
	} else if ub, err = loadUserBasic(ub, c, req.UserID); err != nil {
		// 一个字段都没带: 不落库, 但还是把当前资料回给对方
		return
	}

	user, err := UserLoader(ub)
	if err != nil {
		log.Error("UserLoader failed: uid = %d, %v", req.UserID, err)
		web.Response(c, -1, "服务器内部错误, 请稍后重试")
		return
	}

	rsp := updateUserRsp{User: user}
	web.Response(c, 0, "", sess.Tx, &rsp)
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
