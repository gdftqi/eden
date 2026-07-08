using Lilith.Utils;
using Newtonsoft.Json;
using System;
using System.Threading.Tasks;

namespace Lilith.Core.Ra
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

            [JsonProperty("user_id", NullValueHandling = NullValueHandling.Ignore)]
            public UInt32? UserID { get; set; }

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
                Info = HttpSession.Instance.Seal(info),
            };

            var rsp = await HttpSession.Instance.PostSecureAsync<UserLoginRsp>("/user_login", req);
            Refresh.RefreshToken = rsp.RefreshToken;
            KcpSession.Instance.Init(rsp.Host!, rsp.Conv!.Value, rsp.UserID!.Value, rsp.HostID!.Value, rsp.MacKey!, rsp.AccessToken!);

            return 0;
        }
    }
}
