package utils

import (
	"net/http"

	"github.com/gin-gonic/gin"
)

type HttpRequest struct {
	UserID uint32 `json:"user_id"`
	Data   string `json:"data"`
}

type HttpResponse struct {
	Code  int32  `json:"code"`
	Error string `json:"error,omitempty"`
	Data  any    `json:"data,omitempty"`
}

func WebResponse(c *gin.Context, code int32, err string, data ...any) {
	var d any = nil
	if len(data) > 0 {
		d = data[0]
	}

	c.JSON(http.StatusOK, HttpResponse{
		Code:  code,
		Error: err,
		Data:  d,
	})
}
