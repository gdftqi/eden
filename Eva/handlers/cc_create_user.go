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

// CC 的建号接口. 框架的 /create_user 只建账号(t_user_basic), 这里还要一并写
// 资料(t_user_info)和部门归属(r_user_depart) —— 那些是产品的模型, 不该塞进框架。
//
// 走独立路径而不是覆盖框架那条: gin 同一路径只能挂一个 handler, 而框架路由
// 是在 boot.NewEngine 里注册的, 产品只能加不能改。
const CC_CREATE_USER = "/cc/create_user"

type ccCreateUserReq struct {
	web.BaseRequest

	Username  string  `json:"username"`
	Password  string  `json:"password"`
	Nickname  string  `json:"nickname"`
	PhoneNum  string  `json:"phone_num"`
	DepartIDs []int64 `json:"depart_ids"`
}

type ccCreateUserRsp struct {
	User any `json:"user"`
}

func CCCreateUser(c *gin.Context) {
	req := ccCreateUserReq{}
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

	hash, err := bcrypt.GenerateFromPassword([]byte(req.Password), bcrypt.DefaultCost)
	if err != nil {
		log.Error("bcrypt.GenerateFromPassword failed: %v", err)
		web.Response(c, -1, "服务器内部错误, 请稍后重试")
		return
	}

	tnow := time.Now().Unix()

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

	// 三张表一个事务, 半途失败不留残缺账号
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

	user, err := UserLoader(&ub)
	if err != nil {
		log.Error("UserLoader failed: uid = %d, %v", ub.ID, err)
		web.Response(c, -1, "服务器内部错误, 请稍后重试")
		return
	}

	web.Response(c, 0, "", sess.Tx, &ccCreateUserRsp{User: user})
}
