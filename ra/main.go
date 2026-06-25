package main

import (
	"github.com/gin-gonic/gin"
	"github.com/ra/com"
	"github.com/ra/conf"
	"github.com/ra/handlers"
	"github.com/ra/log"
)

func main() {
	err := conf.Init("config.yml")
	if err != nil {
		log.Fatal(err)
	}
	log.Info("加载配置文件完成")

	err = com.InitRedis(conf.Instance.Redis)
	if err != nil {
		log.Fatal(err)
	}
	log.Info("初始化 reids 完成")

	gin.SetMode(gin.ReleaseMode)
	eng := gin.Default()

	eng.POST(handlers.GET_VERSION, handlers.GetVersion)
	eng.POST(handlers.REGIST_USER, handlers.RegistUser)
	eng.POST(handlers.USER_LOGIN, handlers.UserLogin)

	log.Info("开启服务: %v", conf.Instance.Host)
	eng.Run(conf.Instance.Host)
}
