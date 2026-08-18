package main

// CC 产品的路由登记.
//
// 这个文件只在 CC 分支上存在, main.go 因此可以和框架分支保持一字不差 --
// 框架改动合过来时不会在入口文件上产生冲突。
//
// 资料模型的扩展(昵称/手机号)不在这里, 走 handlers.UserLoader,
// 见 Eva/handlers/cc_user_loader.go 的 init()。

import (
	"github.com/eva/boot"
	"github.com/eva/handlers"
	"github.com/gin-gonic/gin"
)

func init() {
	boot.RegisterExtra(func(eng *gin.Engine) {
		// 组织架构
		eng.POST(handlers.CREATE_DEPART, handlers.CreateDepart)
		eng.POST(handlers.UPDATE_DEPART, handlers.UpdateDepart)
		eng.POST(handlers.GET_ORG, handlers.GetOrg)

		// 建号 / 改资料: 框架那两条只管账号本身, 这两条额外写资料和部门
		eng.POST(handlers.CC_CREATE_USER, handlers.CCCreateUser)
		eng.POST(handlers.CC_UPDATE_USER, handlers.CCUpdateUser)
	})
}
