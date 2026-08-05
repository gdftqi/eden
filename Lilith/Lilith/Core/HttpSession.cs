using System;
using System.Net.Http;
using Newtonsoft.Json;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Lilith.Utils;

namespace Lilith.Core
{
    public class HttpBaseRequest
    {
        [JsonProperty("user_id", NullValueHandling = NullValueHandling.Ignore)]
        public Int64? UserID;

        [JsonProperty("time", NullValueHandling = NullValueHandling.Ignore)]
        public Int64? Time;

        public HttpBaseRequest()
        {
            UserID = HttpSession.Instance.UserID!;
            Time = DateTimeOffset.Now.ToUnixTimeMilliseconds();
        }

        public override string ToString()
        {
            return JsonConvert.SerializeObject(this);
        }
    }

    public class HttpReq
    {
        [JsonProperty("user_id", NullValueHandling = NullValueHandling.Ignore)]
        public Int64? UserID;

        [JsonProperty("data", NullValueHandling = NullValueHandling.Ignore)]
        public string? Data;

        public override string ToString()
        {
            return JsonConvert.SerializeObject(this);
        }
    }

    public class HttpResp
    {
        [JsonProperty("code")]
        public int Code;

        [JsonProperty("error", NullValueHandling = NullValueHandling.Ignore)]
        public string? Error;

        [JsonProperty("data", NullValueHandling = NullValueHandling.Ignore)]
        public string? Data;

        public override string ToString()
        {
            return JsonConvert.SerializeObject(this);
        }
    }

    public class HttpSession
    {
        private static HttpSession instance = new HttpSession();
        public static HttpSession Instance { get { return instance; } }

        private HttpSession()
        {
            hc = new HttpClient();
        }

        public byte[] PK { get { return pk!; } }
        public byte[] SK { get { return sk!; } }
        public byte[] Tx { get { return tx!; } }
        public byte[] Rx { get { return rx!; } }
        public bool Valid { get { return tx != null && rx != null && UserID.HasValue; } }

        public Int64? UserID { get; set; }


        public void SetBaseUrl(string baseUrl)
        {
            this.baseUrl = baseUrl;
        }


        public void SetHttpX25519PK(string b64HttpX25519PK)
        {
            Crypto.X25519KeyGen(out pk, out sk);
            var svrPk = Crypto.Base64DecodeToBytes(b64HttpX25519PK);
            Crypto.X25519KxClient(sk, pk, svrPk, out rx, out tx);
        }


        public string FullUrl(string url)
        {// 拼上 baseUrl, 两边的斜杠都容错
            if (string.IsNullOrEmpty(baseUrl))
            {
                return url;
            }

            if (baseUrl.EndsWith("/"))
            {
                return url.StartsWith("/") ? baseUrl + url.Substring(1) : baseUrl + url;
            }

            return url.StartsWith("/") ? baseUrl + url : baseUrl + "/" + url;
        }


        public async Task<HttpResp> PostAynsc(string url, object? payload = null, CancellationToken cancellationToken = default)
        {
            url = FullUrl(url);

            string jstr = string.Empty;
            if (payload != null)
            {
                jstr = JsonConvert.SerializeObject(payload);
            }

            using (var sctx = new StringContent(jstr, Encoding.UTF8, "application/json"))
            {
                HttpResponseMessage response = await hc.PostAsync(url, sctx, cancellationToken);
                response.EnsureSuccessStatusCode();
                string responseBody = await response.Content.ReadAsStringAsync();

                if (responseBody == null)
                {
                    throw new InvalidOperationException($"响应内容为空");
                }

                HttpResp? rsp = JsonConvert.DeserializeObject<HttpResp>(responseBody);
                if (rsp == null)
                {
                    throw new InvalidOperationException($"无法将响应体反序列化为 {typeof(HttpResp).Name}，内容为空");
                }

                return rsp;
            }
        }


        public string Encrypt(object obj)
        {// 密封, 加密 + 防篡改(认证)
            if (tx == null)
            {
                throw new InvalidOperationException("TxKey 未就绪, 先做密钥交换");
            }

            var nonce = Crypto.RandomNonce();
            var json = Encoding.UTF8.GetBytes(JsonConvert.SerializeObject(obj));
            var cipher = new byte[json.Length + Crypto.XX20_TAG_LEN];
            int clen = Crypto.Encrypt(tx, nonce, json, 0, json.Length, cipher, 0);

            var data = new byte[nonce.Length + clen];
            Buffer.BlockCopy(nonce, 0, data, 0, nonce.Length);
            Buffer.BlockCopy(cipher, 0, data, nonce.Length, clen);
            return Convert.ToBase64String(data);
        }

        
        public T Decrypt<T>(string b64)
        {// base64( nonce(12) || 密文+tag )
            if (rx == null)
            {
                throw new InvalidOperationException("RxKey 未就绪, 先做密钥交换");
            }

            var data = Convert.FromBase64String(b64);
            if (data.Length < Crypto.AEAD_NONCE_LEN + Crypto.XX20_TAG_LEN)
            {
                throw new InvalidOperationException("密文长度不足");
            }

            var nonce = new byte[Crypto.AEAD_NONCE_LEN];
            Buffer.BlockCopy(data, 0, nonce, 0, nonce.Length);
            int clen = data.Length - nonce.Length;
            var plain = new byte[clen];
            int dlen = Crypto.Decrypt(rx, nonce, data, nonce.Length, clen, plain, 0);
            if (dlen < 0)
            {
                throw new InvalidOperationException("解密失败");
            }

            var obj = JsonConvert.DeserializeObject<T>(Encoding.UTF8.GetString(plain, 0, dlen));
            if (obj == null)
            {
                throw new InvalidOperationException("反序列化失败");
            }

            return obj;
        }


        public async Task<TRsp> PostSecureAsync<TRsp>(string url, object req, bool encrypt = false, CancellationToken cancellationToken = default)
        {// 加解密版 POST
            object request = req;

            if (encrypt && UserID.HasValue)
            {
                request = new HttpReq
                {
                    UserID = UserID.Value,
                    Data = Encrypt(req),
                };
            }

            var rsp = await PostAynsc(url, request, cancellationToken);
            if (rsp.Code != 0 || rsp.Data == null)
            {
                throw new Exception(rsp.Error ?? "request failed");
            }

            return Decrypt<TRsp>(rsp.Data);
        }


        public async Task<TRsp> PostFormAsync<TRsp>(string url, MultipartFormDataContent form, CancellationToken cancellationToken = default)
        {// 表单版 POST, 用于上传文件.
         // 服务端那边是 multipart, 解不了 JSON 信封, 所以 user_id/data 由调用方放进表单字段
            HttpResp? rsp;

            using (var response = await hc.PostAsync(FullUrl(url), form, cancellationToken))
            {
                response.EnsureSuccessStatusCode();
                var body = await response.Content.ReadAsStringAsync();

                rsp = JsonConvert.DeserializeObject<HttpResp>(body);
                if (rsp == null)
                {
                    throw new InvalidOperationException("无法将响应体反序列化为 HttpResp");
                }
            }

            if (rsp.Code != 0 || rsp.Data == null)
            {
                throw new Exception(rsp.Error ?? "request failed");
            }

            return Decrypt<TRsp>(rsp.Data);
        }

        private HttpClient hc;
        private string baseUrl = "";
        private byte[]? pk;
        private byte[]? sk;
        private byte[]? tx;
        private byte[]? rx;
    }
}
