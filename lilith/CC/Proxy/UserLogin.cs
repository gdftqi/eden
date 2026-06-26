using lilith.Core;
using lilith.Tools;
using Newtonsoft.Json;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Text;
using System.Threading.Tasks;

namespace CC.Proxy
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

            [JsonProperty("token", NullValueHandling = NullValueHandling.Ignore)]
            public string? Token { get; set; }
        }

        public static async Task<int> POST(string username, string password)
        {
            var raPk = Crypto.Base64DecodeToBytes(Config.HTTP_X25519_PK);

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

            KcpSession.Instance.Init(rsp.Host!, rsp.Conv!.Value, rsp.UserID!.Value, rsp.HostID!.Value, rsp.MacKey!, rsp.Token!);

            return 0;
        }
    }
}
