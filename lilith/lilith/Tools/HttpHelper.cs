using System;
using System.Net.Http;
using Newtonsoft.Json;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace lilith.Tools
{
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

    public class HttpHelper
    {
        private static HttpHelper instance = new HttpHelper();
        public static HttpHelper Instance { get { return instance; } }

        private HttpHelper()
        {
            httpClient = new HttpClient();
        }

        public byte[] PK { get { return pk!; } }
        public byte[] SK { get { return sk!; } }
        public byte[] Tx { get { return txKey!; } }
        public byte[] Rx { get { return rxKey!; } }


        public void Init(string baseUrl, string b64SserverPK)
        {
            this.baseUrl = baseUrl ?? string.Empty;
            Crypto.X25519KeyGen(out pk, out sk);
            var svrPk = Crypto.Base64DecodeToBytes(b64SserverPK);
            Crypto.X25519KxClient(sk, pk, svrPk, out rxKey, out txKey);
        }

        public async Task<HttpResp> PostAynsc(string url, object? payload = null, CancellationToken cancellationToken = default)
        {
            if (!string.IsNullOrEmpty(baseUrl))
            {
                if (baseUrl.EndsWith("/"))
                {
                    if (url.StartsWith("/"))
                    {
                        url = baseUrl + url.Substring(1);
                    }
                    else
                    {
                        url = baseUrl + url;
                    }

                }
                else
                {
                    if (url.StartsWith("/"))
                    {
                        url = baseUrl + url;
                    }
                    else
                    {
                        url = baseUrl + "/" + url;
                    }
                }
            }

            string jstr = string.Empty;
            if (payload != null)
            {
                jstr = JsonConvert.SerializeObject(payload);
            }

            using (var sctx = new StringContent(jstr, Encoding.UTF8, "application/json"))
            {
                HttpResponseMessage response = await httpClient.PostAsync(url, sctx, cancellationToken);
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


        public string Seal(object obj)
        {// 序列化 obj → 用 TxKey 加密 → base64( nonce(12) || 密文+tag )。作请求里要放的密文字段
            if (txKey == null)
            {
                throw new InvalidOperationException("TxKey 未就绪, 先做密钥交换");
            }

            var nonce = Crypto.RandomNonce();
            var json = Encoding.UTF8.GetBytes(JsonConvert.SerializeObject(obj));
            var cipher = new byte[json.Length + Crypto.XX20_TAG_LEN];
            int clen = Crypto.Encrypt(txKey, nonce, json, 0, json.Length, cipher, 0);

            var data = new byte[nonce.Length + clen];
            Buffer.BlockCopy(nonce, 0, data, 0, nonce.Length);
            Buffer.BlockCopy(cipher, 0, data, nonce.Length, clen);
            return Convert.ToBase64String(data);
        }

        
        public T Open<T>(string b64)
        {// base64( nonce(12) || 密文+tag )
            if (rxKey == null)
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
            var plain = new byte[clen];   // 明文 ≤ 密文长
            int dlen = Crypto.Decrypt(rxKey, nonce, data, nonce.Length, clen, plain, 0);
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


        public async Task<TRsp> PostSecureAsync<TRsp>(string url, object req, CancellationToken cancellationToken = default)
        {// 加解密版 POST
            var rsp = await PostAynsc(url, req, cancellationToken);
            if (rsp.Code != 0 || rsp.Data == null)
            {
                throw new Exception(rsp.Error ?? "request failed");
            }
            return Open<TRsp>(rsp.Data);
        }

        private HttpClient httpClient;
        private string baseUrl = "";
        private byte[]? pk;
        private byte[]? sk;
        private byte[]? txKey;
        private byte[]? rxKey;
    }
}
