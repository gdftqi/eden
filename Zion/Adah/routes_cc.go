package main

// CC 产品的路由登记.
//
// 这个文件和 cc/ 包只在 CC 分支上存在, main.go 因此可以和框架分支保持一字不差 --
// 框架改动合过来时不会在入口文件上产生冲突。
//
// 资料模型的扩展(昵称/手机号)不在这里, 走 handlers.UserLoader,
// 见 cc/cc_user_loader.go 的 init()。

import (
	"github.com/adah/cc"
	"github.com/eva/boot"
	"github.com/gin-gonic/gin"
)

func init() {
	boot.RegisterExtra(func(eng *gin.Engine) {
		// 组织架构
		eng.POST(cc.CREATE_DEPART, cc.CreateDepart)
		eng.POST(cc.UPDATE_DEPART, cc.UpdateDepart)
		eng.POST(cc.GET_ORG, cc.GetOrg)

		// 建号 / 改资料: 框架那两条只管账号本身, 这两条额外写资料和部门
		eng.POST(cc.CC_CREATE_USER, cc.CCCreateUser)
		eng.POST(cc.CC_UPDATE_USER, cc.CCUpdateUser)
	})
}
