// Package boot 把 Eva 的启动序列导出给产品侧调用.
//
// Eva 是框架, 不带 main -- 产品自己写入口, 按 Init -> NewEngine -> (加自己的路由) -> Run
// 三步启动. 这样框架加中间件、改初始化顺序时, 产品侧一行都不用动.
package boot

import (
	"github.com/eva/conf"
	"github.com/eva/handlers"
	"github.com/eva/log"
	"github.com/eva/mid"
	"github.com/gin-gonic/gin"
)

// Init 加载配置并把各个中间件拉起来. 任何一步失败都直接终止进程 --
// 缺了其中任何一个, 服务起来了也只是每个请求都失败
func Init(cfgPath string) {
	if err := conf.Init(cfgPath); err != nil {
		log.Fatal(err)
	}
	log.Info("加载配置文件完成")

	if err := mid.InitRedis(conf.Instance.Redis); err != nil {
		log.Fatal(err)
	}
	log.Info("初始化 redis 完成")

	if err := mid.InitEtcd(conf.Instance.Etcd); err != nil {
		log.Fatal(err)
	}
	log.Info("初始化 etcd 完成")

	if err := mid.InitMySQL(conf.Instance.Mysql); err != nil {
		log.Fatal(err)
	}
	log.Info("初始化 mysql 完成")

	if err := mid.InitS3(conf.Instance.S3); err != nil {
		log.Fatal(err)
	}
	log.Info("初始化 s3 完成")
}

// NewEngine 建 gin 引擎并注册框架自己的路由.
// 产品把自己的路由挂在返回的引擎上, 再交给 Run
func NewEngine() *gin.Engine {
	gin.SetMode(gin.ReleaseMode)
	eng := gin.Default()

	// 超过这个大小的 multipart 分片会落临时文件
	eng.MaxMultipartMemory = 8 << 20

	// gin 默认信任所有代理
	eng.SetTrustedProxies(nil)

	eng.POST(handlers.CREATE_USER, handlers.CreateUser)
	eng.POST(handlers.USER_LOGIN, handlers.UserLogin)
	eng.POST(handlers.REFRESH, handlers.Refresh)
	eng.POST(handlers.UPDATE_USER, handlers.UpdateUser)
	eng.POST(handlers.UPDATE, handlers.Update)
	eng.POST(handlers.UPLOAD, handlers.Upload)

	return eng
}

func Run(eng *gin.Engine) {
	log.Info("开启服务: %v", conf.Instance.Host)
	eng.Run(conf.Instance.Host)
}
