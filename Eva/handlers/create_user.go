package handlers

import (
	"time"

	"github.com/eva/dao"
	"github.com/eva/log"
	"github.com/eva/web"
	"github.com/gin-gonic/gin"
	"golang.org/x/crypto/bcrypt"
)

const CREATE_USER = "/create_user"

type createUserReq struct {
	web.BaseRequest

	Username  string  `json:"username"`
	Password  string  `json:"password"`
	Nickname  string  `json:"nickname"`
	PhoneNum  string  `json:"phone_num"`
	DepartIDs []int64 `json:"depart_ids"`
}

type createUserRsp struct {
	dao.User
}

func CreateUser(c *gin.Context) {
	req := createUserReq{}
	sess, err := web.BindW(c, &req)
	if err != nil {
		web.Response(c, -1, err.Error())
		return
	}

	if len(req.Username) < 6 || len(req.Username) > 16 {
		web.Response(c, -1, "用户名无效")
		return
	}

	if len(req.Password) != 64 {
		web.Response(c, -1, "密码无效")
		return
	}

	if len(req.Nickname) == 0 || len(req.Nickname) > 16 {
		web.Response(c, -1, "昵称无效")
		return
	}

	if len(req.PhoneNum) > 15 {
		web.Response(c, -1, "手机号无效")
		return
	}

	tnow := time.Now().Unix()

	count, err := dao.CountUserBasicByUsername(req.Username)
	if err != nil {
		log.Error("CountUserBasicByUsername failed: %v", err)
		web.Response(c, -1, "服务器内部错误, 请稍后重试")
		return
	}

	if count != 0 {
		web.Response(c, -1, "用户名已存在")
		return
	}

	hash, err := bcrypt.GenerateFromPassword([]byte(req.Password), bcrypt.DefaultCost)
	if err != nil {
		log.Error("bcrypt.GenerateFromPassword failed: %v", err)
		web.Response(c, -1, "服务器内部错误, 请稍后重试")
		return
	}

	ub := dao.UserBasic{
		Username:   req.Username,
		Password:   string(hash),
		CreateTime: tnow,
		State:      1,
	}
	err = dao.InsertUserBasic(&ub)
	if err != nil {
		log.Error("InsertUserBasic failed: %v", err)
		web.Response(c, -1, "服务器内部错误, 请稍后重试")
		return
	}

	ui := dao.UserInfo{
		ID:         ub.ID,
		Nickname:   req.Nickname,
		PhoneNum:   req.PhoneNum,
		CreateTime: tnow,
	}
	err = dao.InsertUserInfo(&ui)
	if err != nil {
		log.Error("InsertUserInfo failed: %v", err)
		web.Response(c, -1, "服务器内部错误, 请稍后重试")
		return
	}

	err = dao.UpsertUserDeparts(ub.ID, req.DepartIDs)
	if err != nil {
		log.Error("InsertUserDeparts failed: %v", err)
		web.Response(c, -1, "服务器内部错误, 请稍后重试")
		return
	}

	// 响应用会话的 tx 加密; 新建的 user_id 回给客户端
	rsp := createUserRsp{
		User: dao.User{
			ID:         ub.ID,
			Username:   ub.Username,
			CreateTime: ub.CreateTime,
			Nickname:   req.Nickname,
			PhoneNum:   req.PhoneNum,
			State:      ub.State,
		},
	}
	web.Response(c, 0, "", sess.Tx, &rsp)
}
