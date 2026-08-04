package dao

type User struct {
	ID         int64  `json:"id"`
	Username   string `json:"username"`
	CreateTime int64  `json:"create_time"`
	Nickname   string `json:"nickname"`
	PhoneNum   string `json:"phone_num"`
	State      int64  `json:"state"`
}
