package cc

import (
	"unicode/utf8"

	"github.com/eva/com"
	"github.com/eva/dao"
	"github.com/eva/handlers"
	"github.com/eva/log"
	"github.com/eva/web"
	"github.com/gin-gonic/gin"
	"golang.org/x/crypto/bcrypt"
)

// CC 的改资料接口. 相比框架的 /update_user 多了昵称、手机号、部门三项。
//
// 这里把头像/密码/状态那几段也重写了一遍而不是复用框架的 —— 因为客户端把昵称
// 和头像放在同一个请求里发, 拆成两次调用会出现"昵称改了头像没改"的中间态。
// 代价是这段逻辑两边各有一份, 改框架那份时记得看这里。
const CC_UPDATE_USER = "/cc/update_user"

type ccUpdateUserReq struct {
	web.BaseRequest

	// 指针: nil = 这次不改, "" = 改成空.
	// 用 string 的话这两种情况都是空串, 手机号和头像就只能改不能清
	Nickname *string `json:"nickname,omitempty"`
	PhoneNum *string `json:"phone_num,omitempty"`
	Avatar   *string `json:"avatar,omitempty"`

	OldPassword  string  `json:"old_password"`
	Password     string  `json:"password"`
	State        int64   `json:"state"`
	AddDepartIDs []int64 `json:"add_depart_ids"`
	DelDepartIDs []int64 `json:"del_depart_ids"`
}

type ccUpdateUserRsp struct {
	User any `json:"user"`
}

func CCUpdateUser(c *gin.Context) {
	req := ccUpdateUserReq{}
	sess, err := web.BindW(c, &req)
	if err != nil {
		web.Response(c, -1, err.Error())
		return
	}

	var (
		ub *dao.UserBasic
		ui *UserInfo
	)

	// 昵称是 NOT NULL, 空串不是合法值, 所以不接受清空
	if req.Nickname != nil {
		if utf8.RuneCountInString(*req.Nickname) == 0 || utf8.RuneCountInString(*req.Nickname) > 16 {
			web.Response(c, -1, "无效的昵称")
			return
		}

		if ui, err = ccLoadUserInfo(ui, c, req.UserID); err != nil {
			return
		}

		ui.Nickname = *req.Nickname
	}

	if req.PhoneNum != nil {
		if utf8.RuneCountInString(*req.PhoneNum) > 15 {
			web.Response(c, -1, "无效的手机号")
			return
		}

		if ui, err = ccLoadUserInfo(ui, c, req.UserID); err != nil {
			return
		}

		ui.PhoneNum = *req.PhoneNum
	}

	if req.Avatar != nil {
		if ub, err = loadUserBasic(ub, c, req.UserID); err != nil {
			return
		}

		ub.Avatar = *req.Avatar
	}

	if len(req.Password) > 0 {
		if len(req.OldPassword) != 64 {
			web.Response(c, -1, "原始密码无效")
			return
		}

		if len(req.Password) != 64 {
			web.Response(c, -1, "密码无效")
			return
		}

		if ub, err = loadUserBasic(ub, c, req.UserID); err != nil {
			return
		}

		if bcrypt.CompareHashAndPassword([]byte(ub.Password), []byte(req.OldPassword)) != nil {
			web.Response(c, -1, "原始密码错误")
			return
		}

		hash, err := bcrypt.GenerateFromPassword([]byte(req.Password), bcrypt.DefaultCost)
		if err != nil {
			log.Error("bcrypt.GenerateFromPassword failed: %v", err)
			web.Response(c, -1, "服务器内部错误, 请稍后重试")
			return
		}

		ub.Password = string(hash)
	}

	// 0 表示"这次不改状态", 所以只认 1 和 -1
	if req.State != 0 {
		if req.State != 1 && req.State != -1 {
			web.Response(c, -1, "无效的状态码")
			return
		}

		if ub, err = loadUserBasic(ub, c, req.UserID); err != nil {
			return
		}

		ub.State = req.State
	}

	// 改密码或停用之后把对方踢下线.
	revoke := len(req.Password) > 0 || req.State == -1

	if ub != nil {
		if err = dao.UpdateUserBasic(ub); err != nil {
			log.Error("UpdateUserBasic failed: %v", err)
			web.Response(c, -1, "服务器内部错误, 请稍后重试")
			return
		}

		if revoke {
			if err = com.RevokeUser(uint32(req.UserID)); err != nil {
				log.Error("RevokeUser failed: uid = %d, %v", req.UserID, err)
				web.Response(c, -1, "服务器内部错误, 请稍后重试")
				return
			}
		}
	}

	if ui != nil {
		if err = UpdateUserInfo(ui); err != nil {
			log.Error("UpdateUserInfo failed: %v", err)
			web.Response(c, -1, "服务器内部错误, 请稍后重试")
			return
		}
	}

	if len(req.DelDepartIDs) > 0 {
		if err = DeleteUserDeparts(req.UserID, req.DelDepartIDs); err != nil {
			log.Error("DeleteUserDeparts failed: %v", err)
			web.Response(c, -1, "服务器内部错误, 请稍后重试")
			return
		}
	}

	if len(req.AddDepartIDs) > 0 {
		if err = UpsertUserDeparts(req.UserID, req.AddDepartIDs); err != nil {
			log.Error("UpsertUserDeparts failed: %v", err)
			web.Response(c, -1, "服务器内部错误, 请稍后重试")
			return
		}
	}

	// ub 可能还是 nil(只改了昵称), handlers.UserLoader 要一个 basic 才能拼出完整资料
	if ub, err = loadUserBasic(ub, c, req.UserID); err != nil {
		return
	}

	user, err := handlers.UserLoader(ub)
	if err != nil {
		log.Error("handlers.UserLoader failed: uid = %d, %v", req.UserID, err)
		web.Response(c, -1, "服务器内部错误, 请稍后重试")
		return
	}

	web.Response(c, 0, "", sess.Tx, &ccUpdateUserRsp{User: user})
}

func ccLoadUserInfo(ui *UserInfo, c *gin.Context, userID int64) (*UserInfo, error) {
	if ui != nil {
		return ui, nil
	}

	ui, err := GetUserInfoByID(userID)
	if err != nil {
		log.Error("GetUserInfoByID failed: uid = %d, %v", userID, err)
		respondLoadErr(c, err)
		return nil, err
	}

	return ui, nil
}
