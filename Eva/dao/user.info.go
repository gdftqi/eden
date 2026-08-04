package dao

import "github.com/eva/mid"

type UserInfo struct {
	ID         int64  `json:"id"`
	Nickname   string `json:"nickname"`
	PhoneNum   string `json:"phone_num"`
	CreateTime int64  `json:"create_time"`
}

func InsertUserInfo(obj *UserInfo) error {
	var err error

	if len(obj.PhoneNum) > 0 {
		_, err = mid.Mysql.Exec("INSERT INTO `db_eva`.`t_user_info` (`f_id`, `f_nickname`, `f_phone_num`, `f_create_time`) VALUES (?, ?, ?, ?)",
			obj.ID, obj.Nickname, obj.PhoneNum, obj.CreateTime)
	} else {
		_, err = mid.Mysql.Exec("INSERT INTO `db_eva`.`t_user_info` (`f_id`, `f_nickname`, `f_create_time`) VALUES (?, ?, ?)",
			obj.ID, obj.Nickname, obj.CreateTime)
	}

	return err
}

func UpdateUserInfo(obj *UserInfo) error {
	_, err := mid.Mysql.Exec("UPDATE `db_eva`.`t_user_info` SET `f_nickname` = ?, `f_phone_num` = ? WHERE `f_id` = ?",
		obj.Nickname, obj.PhoneNum, obj.ID)
	return err
}

func GetUserInfoByID(id int64) (*UserInfo, error) {
	row := mid.Mysql.QueryRow("SELECT `f_id`, `f_nickname`, `f_phone_num`, `f_create_time` FROM `db_eva`.`t_user_info` WHERE `f_id` = ?", id)
	obj := &UserInfo{}
	err := row.Scan(&obj.ID, &obj.Nickname, &obj.PhoneNum, &obj.CreateTime)
	if err != nil {
		return nil, err
	}

	return obj, nil
}

func GetUserInfoList() ([]*UserInfo, error) {
	rows, err := mid.Mysql.Query("SELECT `f_id`, `f_nickname`, `f_phone_num`, `f_create_time` FROM `db_eva`.`t_user_info`")
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	var userInfo []*UserInfo
	for rows.Next() {
		obj := &UserInfo{}
		err := rows.Scan(&obj.ID, &obj.Nickname, &obj.PhoneNum, &obj.CreateTime)
		if err != nil {
			return nil, err
		}
		userInfo = append(userInfo, obj)
	}

	return userInfo, nil
}
