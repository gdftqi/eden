using CC.Model;
using Lilith.Core;
using Newtonsoft.Json;
using System;
using System.Threading.Tasks;

namespace CC.Eva
{
    public class UpdateUser
    {
        // 字段都可空 + Ignore: 不传就是"这次不改".
        // 服务端那边对应的是 *string, nil = 不改, "" = 清空
        class Request : HttpBaseRequest
        {
            [JsonProperty("nickname", NullValueHandling = NullValueHandling.Ignore)]
            public string? Nickname { get; set; }

            [JsonProperty("avatar", NullValueHandling = NullValueHandling.Ignore)]
            public string? Avatar { get; set; }

            [JsonProperty("old_password", NullValueHandling = NullValueHandling.Ignore)]
            public string? OldPassword { get; set; }

            [JsonProperty("password", NullValueHandling = NullValueHandling.Ignore)]
            public string? Password { get; set; }
        }


        /// <summary>
        /// 改自己的昵称/头像. 传 null 表示这一项不改.
        /// </summary>
        public static Task<User> Profile(string? nickname, string? avatar)
        {
            return Send(new Request { Nickname = nickname, Avatar = avatar });
        }


        /// <summary>
        /// 改自己的密码. 两个都是客户端 SHA256 之后的 64 位十六进制.
        ///
        /// 成功之后服务端会立刻作废会话和 refresh token, 也就是说
        /// 这之后任何 HTTP 请求都会失败 -- 调用方要把用户送回登录页.
        /// </summary>
        public static Task<User> Password(string oldPassword, string password)
        {
            return Send(new Request { OldPassword = oldPassword, Password = password });
        }


        private static async Task<User> Send(Request req)
        {
            if (!HttpSession.Instance.Valid)
            {
                throw new Exception("用户未登录");
            }

            return await HttpSession.Instance.PostSecureAsync<User>("/update_user", req, true);
        }
    }
}
