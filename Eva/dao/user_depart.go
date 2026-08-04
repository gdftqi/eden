package dao

import "github.com/eva/mid"

func InsertUserDepart(userID, departID int64) error {
	_, err := mid.Mysql.Exec("INSERT INTO db_eva.r_user_depart(user_id,f_depart_id) VALUES(?,?)", userID, departID)
	return err
}
