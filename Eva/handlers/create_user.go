package handlers

import (
	"errors"
	"time"
	"unicode/utf8"

	"github.com/eva/dao"
	"github.com/eva/log"
	"github.com/eva/web"
	"github.com/gin-gonic/gin"
	"github.com/go-sql-driver/mysql"
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

	if len(req.Username) < 5 || len(req.Username) > 16 {
		web.Response(c, -1, "用户名无效")
		return
	}

	if len(req.Password) != 64 {
		web.Response(c, -1, "密码无效")
		return
	}

	if len(req.Nickname) == 0 || utf8.RuneCountInString(req.Nickname) > 16 {
		web.Response(c, -1, "昵称无效")
		return
	}

	if len(req.PhoneNum) > 15 {
		web.Response(c, -1, "手机号无效")
		return
	}

	tnow := time.Now().Unix()

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

	// ID 不在这里填: 自增值要等 t_user_basic 插完才有, InsertUser 里会回填
	ui := dao.UserInfo{
		Nickname:   req.Nickname,
		PhoneNum:   req.PhoneNum,
		CreateTime: tnow,
	}

	if err = dao.InsertUser(&ub, &ui, req.DepartIDs); err != nil {
		// 用户名和手机号上都有唯一索引, 撞了(错误号 1062)是用户填重了, 不是服务出错.
		// 靠索引兜比"先 Count 再 Insert"可靠 -- 后者两步之间有窗口, 并发同名照样撞
		var me *mysql.MySQLError
		if errors.As(err, &me) && me.Number == 1062 {
			web.Response(c, -1, "用户名或手机号已被使用")
			return
		}

		log.Error("InsertUser failed: %v", err)
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
