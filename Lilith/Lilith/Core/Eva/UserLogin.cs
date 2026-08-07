using Lilith.Utils;
using Newtonsoft.Json;
using Newtonsoft.Json.Linq;
using System;
using System.Threading.Tasks;

namespace Lilith.Core.Eva
{
    internal class UserLogin
    {
        class LoginInfo
        {
            [JsonProperty("username", NullValueHandling = NullValueHandling.Ignore)]
            public string? Username { get; set; }

            [JsonProperty("password", NullValueHandling = NullValueHandling.Ignore)]
            public string? Password { get; set; }

            [JsonProperty("time", NullValueHandling = NullValueHandling.Ignore)]
            public Int64? Time { get; set; }
        }

        class UserLoginReq
        {
            [JsonProperty("hpk", NullValueHandling = NullValueHandling.Ignore)]
            public string? HttpPk { get; set; }

            [JsonProperty("kpk", NullValueHandling = NullValueHandling.Ignore)]
            public string? KcpPk { get; set; }

            [JsonProperty("info", NullValueHandling = NullValueHandling.Ignore)]
            public string? Info { get; set; }
        }

        class UserLoginRsp
        {
            [JsonProperty("conv", NullValueHandling = NullValueHandling.Ignore)]
            public UInt32? Conv { get; set; }

            [JsonProperty("host", NullValueHandling = NullValueHandling.Ignore)]
            public string? Host { get; set; }

            [JsonProperty("host_id", NullValueHandling = NullValueHandling.Ignore)]
            public UInt32? HostID { get; set; }

            [JsonProperty("mac_key", NullValueHandling = NullValueHandling.Ignore)]
            public string? MacKey { get; set; }

            [JsonProperty("access_token", NullValueHandling = NullValueHandling.Ignore)]
            public string? AccessToken { get; set; }

            [JsonProperty("refresh_token", NullValueHandling = NullValueHandling.Ignore)]
            public string? RefreshToken { get; set; }

            // 当前用户资料.不解成具体类型, 原样交给上层 -- 见 HttpSession.User
            [JsonProperty("user", NullValueHandling = NullValueHandling.Ignore)]
            public JObject? User { get; set; }
        }

        public static async Task<int> POST(string username, string password)
        {
            var info = new LoginInfo
            {
                Username = username,
                Password = Crypto.Sha256(password),
                Time = DateTimeOffset.UtcNow.ToUnixTimeSeconds(),
            };

            var req = new UserLoginReq
            {
                HttpPk = Crypto.Base64Encode(HttpSession.Instance.PK),
                KcpPk = Crypto.Base64Encode(KcpSession.Instance.PK),
                Info = HttpSession.Instance.Encrypt(info),
            };

            var rsp = await HttpSession.Instance.PostSecureAsync<UserLoginRsp>("/user_login", req);

            // 用户 id 从 user 里取: 服务端不再单独下发 user_id 了.
            // 取不到就没法给包寻址, 也没法带上 HTTP 请求的 user_id, 只能算登录失败
            var userID = rsp.User?["id"]?.Value<Int64>();
            if (userID == null)
            {
                return -1;
            }

            Refresh.RefreshToken = rsp.RefreshToken;
            KcpSession.Instance
                .SetHost(rsp.Host!)
                .SetConv(rsp.Conv!.Value)
                .SetUserID((UInt32)userID.Value)
                .SetGatewayID(rsp.HostID!.Value)
                .SetMacKey(rsp.MacKey!)
                .SetAccessToken(rsp.AccessToken!);

            HttpSession.Instance.UserID = userID.Value;
            HttpSession.Instance.User = rsp.User;

            return 0;
        }
    }
}
