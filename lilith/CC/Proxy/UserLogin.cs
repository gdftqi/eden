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
            public string? Username;

            [JsonProperty("password", NullValueHandling = NullValueHandling.Ignore)]
            public string? Password;

            [JsonProperty("time", NullValueHandling = NullValueHandling.Ignore)]
            public Int64? Time;
        }

        class UserLoginReq
        {
            [JsonProperty("hpk", NullValueHandling = NullValueHandling.Ignore)]
            public string? HttpPk;

            [JsonProperty("kpk", NullValueHandling = NullValueHandling.Ignore)]
            public string? KcpPk;

            [JsonProperty("info", NullValueHandling = NullValueHandling.Ignore)]
            public string? Info;
        }

        class UserLoginRsp
        {
            [JsonProperty("conv", NullValueHandling = NullValueHandling.Ignore)]
            public uint? Conv;

            [JsonProperty("user_id", NullValueHandling = NullValueHandling.Ignore)]
            public uint? UserID;

            [JsonProperty("host", NullValueHandling = NullValueHandling.Ignore)]
            public string? Host;

            [JsonProperty("host_id", NullValueHandling = NullValueHandling.Ignore)]
            public uint? HostID;

            [JsonProperty("mac_key", NullValueHandling = NullValueHandling.Ignore)]
            public string? MacKey;

            [JsonProperty("token", NullValueHandling = NullValueHandling.Ignore)]
            public string? Token;
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
                HttpPk = Crypto.Base64Encode(HttpHelper.Instance.PK),
                KcpPk = Crypto.Base64Encode(KcpSession.Instance.PK),
                Info = HttpHelper.Instance.Seal(info),          // 序列化 + TxKey 加密 + base64
            };

            // 发送 + 校验 code + RxKey 解密 + 反序列化, 一把梭
            var rsp = await HttpHelper.Instance.PostSecureAsync<UserLoginRsp>("/user_login", req);

            KcpSession.Instance.Init(rsp.Host!, rsp.Conv!.Value, rsp.UserID!.Value, rsp.Token!, rsp.HostID!.Value);

            return 0;
        }
    }
}
