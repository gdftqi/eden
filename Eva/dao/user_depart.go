package dao

import "github.com/eva/mid"

func GetDepartUserIDs() (map[int64][]int64, error) {
	rows, err := mid.Mysql.Query("SELECT `f_depart_id`, `f_user_id` FROM `db_eva`.`r_user_depart` WHERE `f_state` = 1 ORDER BY `f_depart_id`, `f_user_id`")
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	m := map[int64][]int64{}
	for rows.Next() {
		var departID, userID int64
		if err = rows.Scan(&departID, &userID); err != nil {
			return nil, err
		}

		m[departID] = append(m[departID], userID)
	}

	return m, rows.Err()
}

func InsertUserDepart(userID, departID int64) error {
	_, err := mid.Mysql.Exec("INSERT INTO db_eva.r_user_depart(user_id,f_depart_id) VALUES(?,?)", userID, departID)
	return err
}
