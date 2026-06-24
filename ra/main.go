package main

import (
	"github.com/gin-gonic/gin"
	"github.com/ra/conf"
	"github.com/ra/log"
)

func main() {
	err := conf.Init("config.yml")
	if err != nil {
		log.Fatal(err)
	}

	gin.SetMode(gin.ReleaseMode)
	eng := gin.Default()
	eng.Run(conf.Instance.Host)
}
