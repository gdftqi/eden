// Adah -- 基础框架的用户登录服务.
package main

import (
	"github.com/eva/boot"
)

func main() {
	boot.Init("config.yml")
	eng := boot.NewEngine()
	boot.Run(eng)
}
