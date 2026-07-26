package dao

import (
	"github.com/eva/mid"
)

type UserBasic struct {
	ID         int64  `json:"id"`
	Username   string `json:"username"`
	Password   string `json:"password"`
	CreateTime int64  `json:"create_time"`
	State      int64  `json:"state"`
}

func InsertUserBasic(obj *UserBasic) error {
	res, err := mid.Mysql.Exec("INSERT INTO db_eva.t_user_basic(f_username, f_password, f_create_time, f_state) VALUES(?,?,?,?)",
		obj.Username, obj.Password, obj.CreateTime, obj.State)
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
	r := mid.Mysql.QueryRow("SELECT f_id, f_username, f_password, f_create_time, f_state FROM db_eva.t_user_basic where f_username = ?",
		username)

	obj := &UserBasic{}
	err := r.Scan(&obj.ID, &obj.Username, &obj.Password, &obj.CreateTime, &obj.State)
	if err != nil {
		return nil, err
	}

	return obj, nil
}

func GetUserBasicList(where string) ([]*UserBasic, error) {
	return nil, nil
}

func UpdateUserBasic(obj *UserBasic) error {
	old, err := GetUserBasicByUsername(obj.Username)
	if err != nil {
		return err
	}

	changed := false

	if len(obj.Password) == 64 && obj.Password != old.Password {
		changed = true
		old.Password = obj.Password
	}

	if obj.State != 0 {
		changed = true
		old.State = obj.State
	}

	if !changed {
		return nil
	}

	mid.Mysql.Exec("UPDATE................")
	return nil
}
