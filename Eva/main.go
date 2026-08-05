package main

import (
	"github.com/eva/conf"
	"github.com/eva/handlers"
	"github.com/eva/log"
	"github.com/eva/mid"
	"github.com/gin-gonic/gin"
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

	err = mid.InitMySQL(conf.Instance.Mysql)
	if err != nil {
		log.Fatal(err)
	}
	log.Info("初始化 mysql 完成")

	err = mid.InitS3(conf.Instance.S3)
	if err != nil {
		log.Fatal(err)
	}
	log.Info("初始化 s3 完成")

	gin.SetMode(gin.ReleaseMode)
	eng := gin.Default()

	// 超过这个大小的 multipart 分片会落临时文件
	eng.MaxMultipartMemory = 8 << 20

	eng.POST(handlers.CREATE_USER, handlers.CreateUser)
	eng.POST(handlers.USER_LOGIN, handlers.UserLogin)
	eng.POST(handlers.REFRESH, handlers.Refresh)
	eng.POST(handlers.UPLOAD, handlers.Upload)
	eng.POST(handlers.CREATE_DEPART, handlers.CreateDepart)

	log.Info("开启服务: %v", conf.Instance.Host)
	eng.Run(conf.Instance.Host)
}
