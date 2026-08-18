package cc

import (
	"database/sql"
	"errors"

	"github.com/eva/dao"
	"github.com/eva/log"
	"github.com/eva/web"
	"github.com/gin-gonic/gin"
)

// 下面两个跟框架 handlers 包里的同名函数是一样的逻辑, 各留一份.
//
// 不去导出框架那两个: 它们是 update_user 的内部细节, 导出就变成了框架的公开
// 契约, 以后框架想改都要顾及产品。复制十几行比多一条跨层依赖划算。

// loadUserBasic 惰性取账号: 已经取过就直接用, 避免一个请求里查好几次库
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
