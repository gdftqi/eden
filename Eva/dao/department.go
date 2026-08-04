package dao

import "github.com/eva/mid"

type Department struct {
	ID    int64  `json:"id"`
	Name  string `json:"name"`
	Desc  string `json:"desc"`
	State int64  `json:"state"`
}

func InsertDepartment(obj *Department) error {
	res, err := mid.Mysql.Exec("INSERT INTO `db_eva`.`t_department` (`f_name`, `f_desc`, `f_create_time`, `f_state`) VALUES (?, ?, UNIX_TIMESTAMP(), ?)",
		obj.Name, obj.Desc, obj.State)
	if err != nil {
		return err
	}

	obj.ID, err = res.LastInsertId()
	if err != nil {
		return err
	}

	return nil
}

func UpdateDepartment(obj *Department) error {
	_, err := mid.Mysql.Exec("UPDATE `db_eva`.`t_department` SET `f_name` = ?, `f_desc` = ?, `f_state` = ? WHERE `f_id` = ?",
		obj.Name, obj.Desc, obj.State, obj.ID)
	return err
}

func DeleteDepartment(id int64) error {
	_, err := mid.Mysql.Exec("DELETE FROM `db_eva`.`t_department` WHERE `f_id` = ?", id)
	return err
}

func GetDepartmentByID(id int64) (*Department, error) {
	row := mid.Mysql.QueryRow("SELECT `f_id`, `f_name`, `f_desc`, `f_state` FROM `db_eva`.`t_department` WHERE `f_id` = ?", id)
	obj := &Department{}
	err := row.Scan(&obj.ID, &obj.Name, &obj.Desc, &obj.State)
	if err != nil {
		return nil, err
	}
	return obj, nil
}

func GetDepartmentList() ([]*Department, error) {
	rows, err := mid.Mysql.Query("SELECT `f_id`, `f_name`, `f_desc`, `f_state` FROM `db_eva`.`t_department`")
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	var departments []*Department
	for rows.Next() {
		obj := &Department{}
		err := rows.Scan(&obj.ID, &obj.Name, &obj.Desc, &obj.State)
		if err != nil {
			return nil, err
		}
		departments = append(departments, obj)
	}
	return departments, nil
}
