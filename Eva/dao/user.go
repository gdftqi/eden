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

func InsertUser(ub *UserBasic, ui *UserInfo, departIDs []int64) error {
	tx, err := mid.Mysql.Begin()
	if err != nil {
		return err
	}

	res, err := tx.Exec("INSERT INTO db_eva.t_user_basic(f_username,f_avatar,f_password,f_create_time,f_last_login,f_state) VALUES(?,?,?,?,?,?)",
		ub.Username, ub.Avatar, ub.Password, ub.CreateTime, ub.LastLogin, ub.State)
	if err != nil {
		tx.Rollback()
		return err
	}

	ub.ID, err = res.LastInsertId()
	if err != nil {
		tx.Rollback()
		return err
	}

	ui.ID = ub.ID

	if len(ui.PhoneNum) > 0 {
		_, err = tx.Exec("INSERT INTO db_eva.t_user_info (f_id,f_nickname,f_phone_num,f_create_time) VALUES (?,?,?,?)",
			ui.ID, ui.Nickname, ui.PhoneNum, ui.CreateTime)
	} else {
		_, err = tx.Exec("INSERT INTO db_eva.t_user_info (f_id,f_nickname,f_create_time) VALUES (?,?,?)",
			ui.ID, ui.Nickname, ui.CreateTime)
	}

	if err != nil {
		tx.Rollback()
		return err
	}

	for _, departID := range departIDs {
		_, err = tx.Exec("INSERT INTO db_eva.r_user_depart(f_user_id,f_depart_id,f_state) VALUES (?,?,1)", ub.ID, departID)
		if err != nil {
			tx.Rollback()
			return err
		}
	}

	return tx.Commit()
}

func GetUserByID(userID int64) (*User, error) {
	r := mid.Mysql.QueryRow("SELECT id, avatar, username, nickname, phone_num, create_time, state FROM db_eva.v_user WHERE id=?", userID)
	user := &User{}
	var vPhone sql.NullString
	err := r.Scan(&user.ID, &user.Avatar, &user.Username, &user.Nickname, &vPhone, &user.CreateTime, &user.State)
	if err != nil {
		return nil, err
	}

	if vPhone.Valid {
		user.PhoneNum = vPhone.String
	}

	return user, nil
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
