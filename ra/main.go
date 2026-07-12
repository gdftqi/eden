package main

import (
	"github.com/gin-gonic/gin"
	"github.com/ra/conf"
	"github.com/ra/handlers"
	"github.com/ra/log"
	"github.com/ra/mid"
)

func main() {
	err := conf.Init("config.yml")
	if err != nil {
		log.Fatal(err)
	}
	log.Info("加载配置文件完成")

	err = mid.InitRedis(conf.Instance.Redis)
	if err != nil {
		log.Fatal(err)
	}
	log.Info("初始化 reids 完成")

	err = mid.InitEtcd(conf.Instance.Etcd)
	if err != nil {
		log.Fatal(err)
	}
	log.Info("初始化 etcd 完成")

	gin.SetMode(gin.ReleaseMode)
	eng := gin.Default()

	eng.POST(handlers.REGIST_USER, handlers.RegistUser)
	eng.POST(handlers.USER_LOGIN, handlers.UserLogin)
	eng.POST(handlers.REFRESH, handlers.Refresh)

	log.Info("开启服务: %v", conf.Instance.Host)
	eng.Run(conf.Instance.Host)
}
