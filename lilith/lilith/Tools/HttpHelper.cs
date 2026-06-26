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
        public static HttpHelper Instance
        {
            get
            {
                return instance;
            }
        }

        private HttpHelper()
        {
            httpClient = new HttpClient();
        }

        public byte[]? TxKey;
        public byte[]? RxKey;

        public void SetBaseUrl(string baseUrl)
        {
            this.baseUrl = baseUrl ?? string.Empty;
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

        private HttpClient httpClient;
        private string baseUrl = "";
    }
}
