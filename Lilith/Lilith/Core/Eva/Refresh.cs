using Lilith.Utils;
using Newtonsoft.Json;
using System;
using System.Diagnostics;
using System.Threading.Tasks;

namespace Lilith.Core.Eva
{
    internal class Refresh
    {
        // 当前会话的 refreshToken: 登录成功后由 UserLogin 设置, 每次 /refresh 成功后滚动更新
        public static string? RefreshToken;

        class RefreshReq
        {
            [JsonProperty("hpk", NullValueHandling = NullValueHandling.Ignore)]
            public string? HttpPk { get; set; }

            [JsonProperty("kpk", NullValueHandling = NullValueHandling.Ignore)]
            public string? KcpPk { get; set; }

            // refreshToken 本身是 RA 加密过的黑盒, 明文放 token 字段即可
            [JsonProperty("token", NullValueHandling = NullValueHandling.Ignore)]
            public string? Token { get; set; }
        }

        class RefreshRsp
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
            public string? NewRefreshToken { get; set; }
        }

        // 用 refreshToken 换一套新会话: 滚动更新 refreshToken + KcpSession.Init.
        // 失败抛异常(refreshToken 失效/顶号 -> 上层应回登录页).
        public static async Task POST()
        {
            Log.Write($"[Refresh] POST() 开始, RefreshToken是否为空={string.IsNullOrEmpty(RefreshToken)}");

            if (string.IsNullOrEmpty(RefreshToken))
            {
                throw new Exception("no refresh token");
            }

            var req = new RefreshReq
            {
                HttpPk = Crypto.Base64Encode(HttpSession.Instance.PK),
                KcpPk = Crypto.Base64Encode(KcpSession.Instance.PK),
                Token = RefreshToken,
            };

            var sw = Stopwatch.StartNew();
            var rsp = await HttpSession.Instance.PostSecureAsync<RefreshRsp>("/refresh", req);
            Log.Write($"[Refresh] POST() HTTP 完成, 耗时={sw.ElapsedMilliseconds}ms, host={rsp.Host}, conv={rsp.Conv}");

            RefreshToken = rsp.NewRefreshToken;
            KcpSession.Instance
                .SetHost(rsp.Host!)
                .SetConv(rsp.Conv!.Value)
                .SetUserID(rsp.UserID!.Value)
                .SetGatewayID(rsp.HostID!.Value)
                .SetMacKey(rsp.MacKey!)
                .SetAccessToken(rsp.AccessToken!);
            Log.Write($"[Refresh] POST() 完成, KcpSession 配置已更新");
        }
    }
}
