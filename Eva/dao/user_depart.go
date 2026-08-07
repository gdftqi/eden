package dao

import "github.com/eva/mid"

func GetDepartUserIDs() (map[int64][]int64, error) {
	rows, err := mid.Mysql.Query("SELECT f_depart_id, f_user_id FROM db_eva.r_user_depart WHERE f_state=1")
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

// UpsertUserDeparts 一个人加进多个部门
func UpsertUserDeparts(userID int64, departIDs []int64) error {
	tx, err := mid.Mysql.Begin()
	if err != nil {
		return err
	}

	for _, departID := range departIDs {
		_, err = tx.Exec("INSERT INTO db_eva.r_user_depart(f_user_id,f_depart_id,f_state) VALUES (?,?,1) ON DUPLICATE KEY UPDATE f_state=1", userID, departID)
		if err != nil {
			tx.Rollback()
			return err
		}
	}

	return tx.Commit()
}

// UpsertUsersDepart 多个人加进同一个部门
func UpsertUsersDepart(departID int64, userIDs []int64) error {
	tx, err := mid.Mysql.Begin()
	if err != nil {
		return err
	}

	for _, userID := range userIDs {
		_, err = tx.Exec("INSERT INTO db_eva.r_user_depart(f_user_id,f_depart_id,f_state) VALUES (?,?,1) ON DUPLICATE KEY UPDATE f_state=1", userID, departID)
		if err != nil {
			tx.Rollback()
			return err
		}
	}

	return tx.Commit()
}

func DeleteUserDeparts(userID int64, departIDs []int64) error {
	tx, err := mid.Mysql.Begin()
	if err != nil {
		return err
	}

	for _, departID := range departIDs {
		_, err = tx.Exec("UPDATE db_eva.r_user_depart SET f_state=-1 WHERE f_user_id=? AND f_depart_id=?", userID, departID)
		if err != nil {
			tx.Rollback()
			return err
		}
	}

	return tx.Commit()
}

func DeleteUsersDepart(departID int64, userIDs []int64) error {
	tx, err := mid.Mysql.Begin()
	if err != nil {
		return err
	}

	for _, userID := range userIDs {
		_, err = tx.Exec("UPDATE db_eva.r_user_depart SET f_state=-1 WHERE f_user_id=? AND f_depart_id=?", userID, departID)
		if err != nil {
			tx.Rollback()
			return err
		}
	}

	return tx.Commit()
}
