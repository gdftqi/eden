package handlers

import (
	"crypto/sha256"
	"encoding/hex"
	"errors"
	"fmt"
	"io"
	"net/http"
	"path/filepath"
	"strings"
	"unicode/utf8"

	"github.com/eva/log"
	"github.com/eva/mid"
	"github.com/eva/web"
	"github.com/gin-gonic/gin"
)

const UPLOAD = "/upload"

const UploadMaxSize = 200 << 20 // 200MB

// 除文件本体外, 表单字段和 multipart 分隔符也占字节, 给一点余量
const uploadSlack = 1 << 20

const uploadSupport = "只支持 png/jpg/jpeg/gif/zip/mp4/mp3"

// 一种允许上传的类型.
// 扩展名 -> (魔数, MIME) 放一张表里: 分成两张迟早会对不上,
// 那时"名叫 .png 内容是 mp4"就能混进来, 还会被打上 image/png 的 MIME
type fileKind struct {
	Mime string

	// 魔数在文件里的偏移. MP4 的前 4 字节是 ftyp box 的长度(随文件变), 真正固定的从第 4 字节起
	Off int

	// 任一命中即可
	Magics [][]byte
}

var uploadKinds = map[string]fileKind{
	".png":  {Mime: "image/png", Magics: [][]byte{{0x89, 'P', 'N', 'G'}}},
	".jpg":  {Mime: "image/jpeg", Magics: [][]byte{{0xFF, 0xD8, 0xFF}}},
	".jpeg": {Mime: "image/jpeg", Magics: [][]byte{{0xFF, 0xD8, 0xFF}}},
	".gif":  {Mime: "image/gif", Magics: [][]byte{[]byte("GIF8")}},

	".zip": {Mime: "application/zip", Magics: [][]byte{
		{'P', 'K', 0x03, 0x04},
		{'P', 'K', 0x05, 0x06}, // 空压缩包
		{'P', 'K', 0x07, 0x08}, // 分卷
	}},

	".mp4": {Mime: "video/mp4", Off: 4, Magics: [][]byte{[]byte("ftyp")}},

	// 带 ID3v2 标签的以 "ID3" 开头; 没标签的直接是帧同步头(前 11 位全 1),
	// 第二字节按 MPEG 版本/层/CRC 变化, 这几个覆盖了实际能见到的 Layer III
	".mp3": {Mime: "audio/mpeg", Magics: [][]byte{
		[]byte("ID3"),
		{0xFF, 0xFB}, {0xFF, 0xFA}, // MPEG-1 Layer III
		{0xFF, 0xF3}, {0xFF, 0xF2}, // MPEG-2 Layer III
		{0xFF, 0xE3}, {0xFF, 0xE2}, // MPEG-2.5 Layer III
	}},
}

// 够放下 Off 最大(4) + 魔数最长(4)
const uploadHeadLen = 8

type uploadReq struct {
	web.BaseRequest

	Name string `json:"name"`
	Size int64  `json:"size"`
}

type uploadRsp struct {
	Url string `json:"url"`
}

func Upload(c *gin.Context) {
	if c.Request.ContentLength > UploadMaxSize+uploadSlack {
		web.Response(c, -1, "文件大小超出限制")
		return
	}

	c.Request.Body = http.MaxBytesReader(c.Writer, c.Request.Body, UploadMaxSize+uploadSlack)

	if _, err := c.MultipartForm(); err != nil {
		var maxErr *http.MaxBytesError
		if errors.As(err, &maxErr) {
			web.Response(c, -1, "文件大小超出限制")
			return
		}

		log.Error("Upload: 解析表单失败: %v", err)
		web.Response(c, -1, "请求格式错误")
		return
	}

	req := uploadReq{}
	sess, err := web.BindWForm(c, &req)
	if err != nil {
		web.Response(c, -1, err.Error())
		return
	}

	if utf8.RuneCountInString(req.Name) == 0 || utf8.RuneCountInString(req.Name) > 128 {
		web.Response(c, -1, "文件名无效")
		return
	}

	ext := strings.ToLower(filepath.Ext(req.Name))
	kind, ok := uploadKinds[ext]
	if !ok {
		web.Response(c, -1, uploadSupport)
		return
	}

	fh, err := c.FormFile("file")
	if err != nil {
		log.Error("Upload: 取文件失败: uid = %d, %v", sess.UserID, err)
		web.Response(c, -1, "没有收到文件")
		return
	}

	if fh.Size <= 0 || fh.Size > UploadMaxSize {
		web.Response(c, -1, "文件大小超出限制")
		return
	}

	if req.Size != fh.Size {
		log.Error("Upload: 大小不符: uid = %d, 申报 %d, 实到 %d", sess.UserID, req.Size, fh.Size)
		web.Response(c, -1, "无效的数据")
		return
	}

	f, err := fh.Open()
	if err != nil {
		log.Error("Upload: 打开文件失败: uid = %d, %v", sess.UserID, err)
		web.Response(c, -1, "服务器内部错误, 请稍后重试")
		return
	}
	defer f.Close()

	head := make([]byte, uploadHeadLen)
	n, err := io.ReadFull(f, head)
	if err != nil && !errors.Is(err, io.ErrUnexpectedEOF) && !errors.Is(err, io.EOF) {
		log.Error("Upload: 读文件头失败: uid = %d, %v", sess.UserID, err)
		web.Response(c, -1, "服务器内部错误, 请稍后重试")
		return
	}

	if !kind.match(head[:n]) {
		web.Response(c, -1, "文件内容与扩展名不符")
		return
	}

	// 上面读掉了几个字节, 交给存储前要退回开头
	if _, err = f.Seek(0, io.SeekStart); err != nil {
		log.Error("Upload: 回退文件失败: uid = %d, %v", sess.UserID, err)
		web.Response(c, -1, "服务器内部错误, 请稍后重试")
		return
	}

	url, err := putObject(ext, kind.Mime, f, fh.Size)
	if err != nil {
		log.Error("Upload: 存储失败: uid = %d, %v", sess.UserID, err)
		web.Response(c, -1, "上传失败, 请稍后重试")
		return
	}

	rsp := uploadRsp{
		Url: url,
	}
	web.Response(c, 0, "", sess.Tx, &rsp)
}

func (this_ *fileKind) match(head []byte) bool {
	for _, m := range this_.Magics {
		end := this_.Off + len(m)
		if len(head) >= end && string(head[this_.Off:end]) == string(m) {
			return true
		}
	}
	return false
}

// putObject 用内容的 sha256 当对象名.
func putObject(ext, mime string, f io.ReadSeeker, size int64) (string, error) {
	h := sha256.New()
	if _, err := io.Copy(h, f); err != nil {
		return "", err
	}

	if _, err := f.Seek(0, io.SeekStart); err != nil {
		return "", err
	}

	key := fmt.Sprintf("%s%s", hex.EncodeToString(h.Sum(nil)), ext)
	ok, err := mid.S3Exists(key)
	if err != nil {
		return "", err
	}

	if ok {
		return mid.S3Url(key), nil
	}

	return mid.S3Put(key, f, size, mime)
}
