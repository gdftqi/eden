package main

import (
	"github.com/gin-gonic/gin"
	"github.com/ra/conf"
	"github.com/ra/handlers"
	"github.com/ra/log"
)

func main() {
	err := conf.Init("config.yml")
	if err != nil {
		log.Fatal(err)
	}

	gin.SetMode(gin.ReleaseMode)
	eng := gin.Default()

	eng.POST(handlers.GET_VERSION, handlers.GetVersion)
	eng.POST(handlers.REGIST_USER, handlers.RegistUser)
	eng.POST(handlers.USER_LOGIN, handlers.UserLogin)

	eng.Run(conf.Instance.Host)
}
