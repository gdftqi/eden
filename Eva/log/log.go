package log

import (
	"bufio"
	"fmt"
	"os"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"sync"
	"time"
)

const (
	levelDebug = iota + 1 // 调试级别
	levelInfo             // 信息级别
	levelWarn             // 警告级别
	levelError            // 错误级别
	levelFatal            // 严重级别
)

var (
	logPath = "" // 日志路径（从 HYDRA_LOG_PATH 读取或通过 SetPath 设置）
	// level值 映射 名称
	lvmap = map[int]string{
		levelDebug: "DEBUG",
		levelInfo:  "INFO",
		levelError: "ERROR",
		levelFatal: "FATAL",
		levelWarn:  "WARN",
	}

	lvfnmap = map[int]string{}   // 当前level值 对应的文件名
	lvfmap  = map[int]*os.File{} // 当前level值 对应的文件句柄
	fmtx    = sync.Mutex{}

	// 管道日志相关
	logChan  chan string   // stdout 打印通道
	fileChan chan logEntry // 文件落盘通道
	wg       sync.WaitGroup
	once     sync.Once
	stop     = make(chan struct{})
	// 全局最小日志级别门限（默认 DEBUG，生产可通过环境变量提升门限）
	minLevel = levelDebug
)

// logEntry 日志条目
type logEntry struct {
	lv      int       // 日志级别
	ts      time.Time // 时间戳
	content string    // 日志内容
}

// 初始化管道日志处理器
func initPipeLogger() {
	once.Do(func() {
		// 读取环境配置
		bufSize := 10000
		if v := os.Getenv("HYDRA_LOG_BUF"); len(v) > 0 {
			if n, err := strconv.Atoi(v); err == nil && n > 0 {
				bufSize = n
			}
		}
		if p := os.Getenv("HYDRA_LOG_PATH"); len(p) > 0 {
			SetPath(p)
		}
		if lvl := os.Getenv("HYDRA_LOG_LEVEL"); len(lvl) > 0 {
			SetLevelByName(lvl)
		}

		logChan = make(chan string, bufSize)
		fileChan = make(chan logEntry, bufSize)

		// stdout 打印线程
		wg.Add(1)
		go func() {
			defer wg.Done()

			for {
				select {
				case logMsg := <-logChan:
					fmt.Print(logMsg)

				case <-stop:
					for {
						select {
						case logMsg := <-logChan:
							fmt.Print(logMsg)

						default:
							return
						}
					}
				}
			}
		}()

		// 文件落盘线程（批量刷盘）
		wg.Add(1)
		go runFileWriter()
	})
}

// ASSERT 断言
// cond: 条件
// arg: 参数
// 如果条件不成立，则panic
func ASSERT(cond bool, arg ...any) {
	if !cond {
		panic(buildContent(arg...))
	}
}

// SetLevel 设置全局最小日志级别门限
// 低于该级别的日志将被丢弃
func SetLevel(lv int) {
	minLevel = lv
}

// SetLevelByName 通过名称设置日志级别门限（DEBUG/INFO/WARN/ERROR/FATAL）
func SetLevelByName(name string) {
	switch strings.ToUpper(strings.TrimSpace(name)) {
	case "DEBUG":
		minLevel = levelDebug
	case "INFO":
		minLevel = levelInfo
	case "WARN", "WARNING":
		minLevel = levelWarn
	case "ERROR":
		minLevel = levelError
	case "FATAL":
		minLevel = levelFatal
	}
}

// Done 关闭日志系统
func Done() {
	close(stop)
	wg.Wait()
}

func SetPath(path string) {
	n := len(path)

	if n == 0 {
		return
	}

	if path[n-1:n] == "/" {
		logPath = path[:n-1]
	}
	logPath = path

	_, err := os.Stat(logPath)
	if err != nil {
		if os.IsNotExist(err) {
			err = os.Mkdir(logPath, 0755)
			if err != nil {
				panic(err)
			}
		}
	}
}

func Debug(args ...interface{}) {
	base(levelDebug, args...)
}

func Info(args ...interface{}) {
	base(levelInfo, args...)
}

func Warn(args ...interface{}) {
	base(levelWarn, args...)
}

func Error(args ...interface{}) {
	base(levelError, args...)
}

func Fatal(args ...interface{}) {
	base(levelFatal, args...)
	Done()
	os.Exit(1)
}

func getFile(lv int, tn time.Time) *os.File {
	fmtx.Lock()
	defer fmtx.Unlock()

	if len(logPath) == 0 {
		return nil
	}

	dir := filepath.Clean(logPath)
	_ = os.MkdirAll(dir, 0755)
	fname := filepath.Join(dir, fmt.Sprintf("%s.%s", tn.Format("2006-01-02"), lvmap[lv]))
	if fname != lvfnmap[lv] {
		if lvfmap[lv] != nil {
			lvfmap[lv].Sync()
			lvfmap[lv].Close()
		}
	}

	lvfnmap[lv] = fname
	var err error

	lvfmap[lv], err = os.OpenFile(fname, os.O_WRONLY|os.O_APPEND|os.O_CREATE, 0755)
	if err != nil {
		fmt.Println(err)
		return nil
	}

	return lvfmap[lv]
}

func buildContent(args ...any) string {
	if len(args) == 1 {
		return fmt.Sprintf("%v", args[0])
	}

	if v, ok := args[0].(string); ok {
		return fmt.Sprintf(v, args[1:]...)
	}

	panic("first params must be string")
}

func base(lv int, args ...any) {
	// 门限过滤：丢弃低于最小级别的日志
	if lv < minLevel {
		return
	}
	_, file, line, _ := runtime.Caller(2)
	content := buildContent(args...)
	tn := time.Now()

	// 初始化管道日志处理器
	initPipeLogger()

	// 构建日志消息
	logMsg := fmt.Sprintf("[%s %s %s:%d] %s\n",
		lvmap[lv],
		tn.Format("2006-01-02 15:04:05.000000"),
		file,
		line,
		content)

	// 异步落盘（如果配置了路径，且非 DEBUG）
	if len(logPath) > 0 && lv != levelDebug && fileChan != nil {
		select {
		case fileChan <- logEntry{lv: lv, ts: tn, content: logMsg}:
		default:
			// 降级：通道满则直接打印到 stdout，避免阻塞
			fmt.Print(logMsg)
		}
	}

	// 发送到管道
	select {
	case logChan <- logMsg:
		// 成功发送到管道
	default:
		// 管道满了，直接输出
		fmt.Print(logMsg)
	}
}

// 文件落盘 goroutine，实现简单批量刷盘
func runFileWriter() {
	defer wg.Done()
	flushEvery := 100 * time.Millisecond
	if v := os.Getenv("HYDRA_LOG_FLUSH_INTERVAL_MS"); len(v) > 0 {
		if n, err := strconv.Atoi(v); err == nil && n > 0 {
			flushEvery = time.Duration(n) * time.Millisecond
		}
	}
	batchSize := 50
	if v := os.Getenv("HYDRA_LOG_BATCH_SIZE"); len(v) > 0 {
		if n, err := strconv.Atoi(v); err == nil && n > 0 {
			batchSize = n
		}
	}

	type writerWrap struct{ w *bufio.Writer }
	writers := map[int]*writerWrap{}
	getWriter := func(lv int, tn time.Time) *bufio.Writer {
		if ww := writers[lv]; ww != nil {
			return ww.w
		}
		f := getFile(lv, tn)
		if f == nil {
			return nil
		}
		ww := &writerWrap{w: bufio.NewWriterSize(f, 64*1024)}
		writers[lv] = ww
		return ww.w
	}

	// flushAll 刷新所有写入器
	flushAll := func() {
		for _, ww := range writers {
			if ww != nil && ww.w != nil {
				_ = ww.w.Flush()
			}
		}
	}

	ticker := time.NewTicker(flushEvery)
	defer ticker.Stop()

	buf := make([]logEntry, 0, batchSize)

	for {
		select {
		case e := <-fileChan:
			buf = append(buf, e)
			if len(buf) >= batchSize {
				for _, it := range buf {
					if w := getWriter(it.lv, it.ts); w != nil {
						_, _ = w.WriteString(it.content)
					}
				}

				flushAll()
				buf = buf[:0]
			}

		case <-ticker.C:
			if len(buf) > 0 {
				for _, it := range buf {
					if w := getWriter(it.lv, it.ts); w != nil {
						_, _ = w.WriteString(it.content)
					}
				}

				flushAll()
				buf = buf[:0]
			}

		case <-stop:
			// drain
			for {
				select {
				case it := <-fileChan:
					buf = append(buf, it)

				default:
					for _, it := range buf {
						if w := getWriter(it.lv, it.ts); w != nil {
							_, _ = w.WriteString(it.content)
						}
					}
					flushAll()
					return
				}
			}
		}
	}
}
