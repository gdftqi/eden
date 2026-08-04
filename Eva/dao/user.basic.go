package dao

import (
	"github.com/eva/mid"
)

type UserBasic struct {
	ID         int64  `json:"id"`
	Username   string `json:"username"`
	Avatar     string `json:"avatar"`
	Password   string `json:"password"`
	CreateTime int64  `json:"create_time"`
	LastLogin  int64  `json:"last_login"`
	State      int64  `json:"state"`
}

func InsertUserBasic(obj *UserBasic) error {
	res, err := mid.Mysql.Exec("INSERT INTO db_eva.t_user_basic(f_username,f_avatar,f_password,f_create_time,f_last_login,f_state) VALUES(?,?,?,?,?,?)",
		obj.Username, obj.Avatar, obj.Password, obj.CreateTime, obj.LastLogin, obj.State)
	if err != nil {
		return err
	}

	obj.ID, err = res.LastInsertId()
	if err != nil {
		return err
	}

	return nil
}

func CountUserBasicByUsername(username string) (int, error) {
	r := mid.Mysql.QueryRow("SELECT COUNT(1) FROM db_eva.t_user_basic where f_username=?", username)
	count := 0
	err := r.Scan(&count)
	if err != nil {
		return -1, err
	}

	return count, nil
}

func GetUserBasicByUsername(username string) (*UserBasic, error) {
	r := mid.Mysql.QueryRow("SELECT f_id,f_username,f_avatar,f_password,f_create_time,f_last_login,f_state FROM db_eva.t_user_basic where f_username = ?",
		username)

	obj := &UserBasic{}
	err := r.Scan(&obj.ID, &obj.Username, &obj.Avatar, &obj.Password, &obj.CreateTime, &obj.LastLogin, &obj.State)
	if err != nil {
		return nil, err
	}

	return obj, nil
}

func GetUserBasicList(where string) ([]*UserBasic, error) {
	rows, err := mid.Mysql.Query("SELECT f_id,f_username,f_avatar,f_password,f_create_time,f_last_login,f_state FROM db_eva.t_user_basic " + where)
	if err != nil {
		return nil, err
	}

	defer rows.Close()

	dataList := []*UserBasic{}
	for rows.Next() {
		obj := &UserBasic{}
		err = rows.Scan(&obj.ID, &obj.Username, &obj.Avatar, &obj.Password, &obj.CreateTime, &obj.LastLogin, &obj.State)
		if err != nil {
			return nil, err
		}

		dataList = append(dataList, obj)
	}

	return dataList, nil
}

func UpdateUserBasic(obj *UserBasic) error {
	raw, err := GetUserBasicByUsername(obj.Username)
	if err != nil {
		return err
	}

	changed := false

	if len(obj.Avatar) > 0 && obj.Avatar != raw.Avatar {
		changed = true
		raw.Avatar = obj.Avatar
	}

	if len(obj.Password) == 64 && obj.Password != raw.Password {
		changed = true
		raw.Password = obj.Password
	}

	if obj.LastLogin > 0 && obj.LastLogin != raw.LastLogin {
		changed = true
		raw.LastLogin = obj.LastLogin
	}

	if obj.State != 0 {
		changed = true
		raw.State = obj.State
	}

	if !changed {
		return nil
	}

	_, err = mid.Mysql.Exec("UPDATE db_eva.t_user_basic SET f_password=?,f_last_login=?,f_state=? WHERE f_id=?",
		raw.Password, raw.LastLogin, raw.State, raw.ID)
	return err
}
