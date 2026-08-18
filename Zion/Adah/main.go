// Adah -- 基础框架的用户登录服务.
//
// 它自己不含任何业务逻辑: 全部能力来自 Eva 框架, 这里只是把 Eva 装配成一个
// 可部署的进程. 产品要加自己的接口时, 复制这个 main 到自己的服务里,
// 在 NewEngine() 和 Run() 之间挂路由即可, 不必改 Eva 一行.
package main

import (
	"github.com/eva/boot"
)

func main() {
	boot.Init("config.yml")
	eng := boot.NewEngine()
	boot.Run(eng)
}
