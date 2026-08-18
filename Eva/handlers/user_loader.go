package handlers

import (
	"github.com/eva/dao"
)

// UserLoader 决定响应里的 user 字段长什么样.
//
// 框架只认账号本身(t_user_basic: username / avatar / state),昵称、手机号、部门
// 这些是产品的资料模型, 框架不该知道。产品有自己的资料表时, 在 init() 里把这个
// 变量换掉即可 —— 登录 / 建号 / 改资料三处响应会一起跟着变, 不必各改一遍。
//
// @note 只在启动阶段(init / main 里 Run 之前)赋值。运行期换会和请求并发读竞争。
var UserLoader = func(ub *dao.UserBasic) (any, error) {
	return ub, nil
}
