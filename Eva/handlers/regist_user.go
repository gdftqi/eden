package handlers

import (
	"math"
	"net/http"
	"time"

	"github.com/eva/dao"
	"github.com/eva/log"
	"github.com/eva/utils"
	"github.com/gin-gonic/gin"
	"golang.org/x/crypto/bcrypt"
)

const REGIST_USER = "/regist_user"

type registUserReq struct {
	Username string `json:"username"`
	Password string `json:"password"`
	Time     int64  `json:"time"`
}

func RegistUser(c *gin.Context) {
	req := registUserReq{}
	err := c.BindJSON(&req)
	if err != nil {
		log.Error("请求格式错误: %v", err)
		c.JSON(http.StatusBadRequest, nil)
		return
	}

	if len(req.Username) < 8 || len(req.Username) > 16 {
		utils.WebResponse(c, -1, "用户名无效")
		return
	}

	if len(req.Password) != 64 {
		utils.WebResponse(c, -1, "密码无效")
		return
	}

	tnow := time.Now().Unix()
	if math.Abs(float64(req.Time)-float64(tnow)) > 5 {
		utils.WebResponse(c, -1, "请求超时, 请重试")
		return
	}

	count, err := dao.CountUserBasicByUsername(req.Username)
	if err != nil {
		log.Error("CountUserBasicByUsername failed: %v", err)
		utils.WebResponse(c, -1, "服务器内部错误, 请稍后重试")
		return
	}

	if count != 0 {
		utils.WebResponse(c, -1, "用户名已存在")
		return
	}

	hash, err := bcrypt.GenerateFromPassword([]byte(req.Password), bcrypt.DefaultCost)
	if err != nil {
		log.Error("bcrypt.GenerateFromPassword failed: %v", err)
		utils.WebResponse(c, -1, "服务器内部错误, 请稍后重试")
		return
	}

	user := dao.UserBasic{
		Username:   req.Username,
		Password:   string(hash),
		CreateTime: tnow,
		State:      1,
	}
	err = dao.InsertUserBasic(&user)
	if err != nil {
		log.Error("InsertUserBasic failed: %v", err)
		utils.WebResponse(c, -1, "服务器内部错误, 请稍后重试")
		return
	}

	utils.WebResponse(c, 0, "")
}
