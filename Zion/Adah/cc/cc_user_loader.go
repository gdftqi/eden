package cc

import (
	"database/sql"
	"errors"

	"github.com/eva/dao"
	"github.com/eva/handlers"
)

// CC 产品的资料模型: 框架只认 t_user_basic, 昵称和手机号在 t_user_info,
// 这里把两张表拼成客户端要的形状(等价于查 v_user)。
//
// 装在 handlers.UserLoader 上之后, 登录 / 建号 / 改资料三处响应都会带上完整资料,
// 框架侧一行都不用改。
func init() {
	handlers.UserLoader = ccUserLoader
}

func ccUserLoader(ub *dao.UserBasic) (any, error) {
	u := &User{
		ID:         ub.ID,
		Avatar:     ub.Avatar,
		Username:   ub.Username,
		CreateTime: ub.CreateTime,
		State:      ub.State,
	}

	ui, err := GetUserInfoByID(ub.ID)
	if err != nil {
		// 资料行缺失不是错误: 框架的 /create_user 只写 t_user_basic,
		// 没走 CC 的建号流程时就没有这一行, 此时昵称手机号留空即可
		if errors.Is(err, sql.ErrNoRows) {
			return u, nil
		}
		return nil, err
	}

	u.Nickname = ui.Nickname
	u.PhoneNum = ui.PhoneNum
	return u, nil
}
