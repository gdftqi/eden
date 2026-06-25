package mid

import (
	"database/sql"
	"time"

	"github.com/go-sql-driver/mysql"
	"github.com/ra/log"
)

var Mysql *sql.DB

type MysqlConfig struct {
	Addr     string `yaml:"addr"`
	Username string `yaml:"username"`
	Password string `yaml:"password"`
	Timeout  int64  `yaml:"timeout"`
}

func InitMySQL(c *MysqlConfig) error {
	if c == nil {
		log.Fatal("c is nil")
	}

	mc := &mysql.Config{
		Addr:                 c.Addr,
		User:                 c.Username,
		Passwd:               c.Password,
		Net:                  "tcp",
		Loc:                  time.Local,
		Collation:            "utf8mb4_general_ci",
		MaxAllowedPacket:     64 << 20,
		AllowNativePasswords: true,
		CheckConnLiveness:    true,
		ParseTime:            true,
		Timeout:              time.Duration(c.Timeout) * time.Second,
	}

	db, err := sql.Open("mysql", mc.FormatDSN())
	if err != nil {
		return err
	}

	err = db.Ping()
	if err != nil {
		db.Close()
		return err
	}

	Mysql = db
	return nil
}
