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
            Config.Init();
            var pk = Crypto.Base64DecodeToBytes(Config.HTTP_X25519_PK);
            Crypto.KxClient(Config.HttpSk, Config.HttpPk, pk, out HttpHelper.Instance.RxKey, out HttpHelper.Instance.TxKey);

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
            req.KcpPk = Crypto.Base64Encode(KcpSession.Instance.PK);
            req.Info = Crypto.Base64Encode(data);


            var rsp = await HttpHelper.Instance.PostAynsc("/user_login", req);
            if (rsp.Code != 0 || rsp.Data == null)
            {
                throw new Exception(rsp.Error);
            }

            data = Crypto.Base64DecodeToBytes(rsp.Data);
            var rnonce = data.AsSpan(0, Crypto.AEAD_NONCE_LEN).ToArray();
            var rcipher = data.AsSpan(Crypto.AEAD_NONCE_LEN).ToArray();
            var plain = new byte[rcipher.Length];
            int dlen = Crypto.Decrypt(HttpHelper.Instance.RxKey, rnonce, rcipher, 0, rcipher.Length, plain, 0);
            if (dlen < 0)
            {
                throw new Exception("login response decrypt failed");
            }
            UserLoginRsp? ulRsp = JsonConvert.DeserializeObject<UserLoginRsp>(Encoding.UTF8.GetString(plain, 0, dlen));
            if (ulRsp == null)
            {
                throw new Exception("server is fake");
            }

            KcpSession.Instance.Init(ulRsp.Host!, ulRsp.Conv!.Value, ulRsp.UserID!.Value, ulRsp.Token!, ulRsp.HostID!.Value);

            return 0;
        }
    }
}
