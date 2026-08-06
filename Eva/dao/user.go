package dao

import (
	"database/sql"

	"github.com/eva/mid"
)

type User struct {
	ID         int64  `json:"id"`
	Avatar     string `json:"avatar"`
	Username   string `json:"username"`
	CreateTime int64  `json:"create_time"`
	Nickname   string `json:"nickname"`
	PhoneNum   string `json:"phone_num"`
	State      int64  `json:"state"`
}

func GetUserList() ([]*User, error) {
	rows, err := mid.Mysql.Query("SELECT id, avatar, username, nickname, phone_num, create_time, state FROM db_eva.v_user")
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	var (
		users  []*User
		vPhone sql.NullString
	)
	for rows.Next() {
		var user User
		err := rows.Scan(&user.ID, &user.Avatar, &user.Username, &user.Nickname, &vPhone, &user.CreateTime, &user.State)
		if err != nil {
			return nil, err
		}
		if vPhone.Valid {
			user.PhoneNum = vPhone.String
		}
		users = append(users, &user)
	}

	return users, nil
}
