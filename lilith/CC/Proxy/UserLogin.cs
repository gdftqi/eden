using Avalonia.Controls.Notifications;
using lilith.Tools;
using Newtonsoft.Json;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
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

        public static async Task<int> POST(string username, string password)
        {
            Config.Init();
            var pk = Crypto.Base64DecodeToBytes(Config.HTTP_X25519_PK);
            Crypto.KxClient(pk, out HttpHelper.Instance.RxKey, out HttpHelper.Instance.TxKey);

            LoginInfo info = new LoginInfo();
            info.Username = username;
            info.Password = Crypto.Sha256(password);
            info.Time = DateTimeOffset.UtcNow.ToUnixTimeSeconds();

            var nonce = Crypto.RandomNonce();
            var bJson = Encoding.UTF8.GetBytes(JsonConvert.SerializeObject(info));
            byte[] cipher = new byte[1024];
            int cipherLen = Crypto.Encrypt(HttpHelper.Instance.TxKey, nonce, bJson, 0, bJson.Length, cipher, 0);
            cipher = cipher.AsSpan(0, cipherLen).ToArray();

            var data = new byte[nonce.Length + cipher.Length];
            nonce.CopyTo(data.AsSpan());
            cipher.CopyTo(data.AsSpan(nonce.Length));

            UserLoginReq req = new UserLoginReq();
            req.HttpPk = Crypto.Base64Encode(Config.HttpPk);
            req.KcpPk = Crypto.Base64Encode(Config.KcpPk);
            req.Info = Crypto.Base64Encode(data);


            var rsp = await HttpHelper.Instance.PostAynsc("/user_login", req);
            if (rsp.Code != 0)
            {
                throw new Exception(rsp.Error);
            }
            Debug.WriteLine(rsp.ToString());

            return 0;
        }
    }
}
