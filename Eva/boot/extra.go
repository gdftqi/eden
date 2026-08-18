package boot

import (
	"github.com/gin-gonic/gin"
)

var extras []func(*gin.Engine)

// RegisterExtra 登记产品自己的路由, NewEngine 会在框架路由之后统一应用.
//
// 目的是让产品**只新增文件**就能扩展服务: 入口 main.go 在框架分支和产品分支上
// 保持一字不差, 产品把自己的路由写在独有的文件里, 用 init() 登记进来。
// 否则两边的 main.go 内容不同, 每次合并框架改动都要手工解冲突。
//
//	func init() {
//	    boot.RegisterExtra(func(eng *gin.Engine) {
//	        eng.POST("/xxx", XxxHandler)
//	    })
//	}
//
// @note 只在 init() / NewEngine 之前调用, 之后再登记不会生效。
func RegisterExtra(f func(*gin.Engine)) {
	extras = append(extras, f)
}
