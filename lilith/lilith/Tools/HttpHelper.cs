using System;
using System.Collections.Generic;
using System.Net.Http;
using Newtonsoft.Json;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace lilith.Tools
{
    public class HttpHelper
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

        private HttpClient httpClient;
        public async Task<HttpResp> PostAynsc(string url, object? payload = null, CancellationToken cancellationToken = default)
        {
            HttpContent sctx;
            if (payload != null)
            {
                sctx = new StringContent(JsonConvert.SerializeObject(payload), Encoding.UTF8, "application/json");
            }
            else
            {
                sctx = new ByteArrayContent(Array.Empty<byte>());
                sctx.Headers.ContentType = new System.Net.Http.Headers.MediaTypeHeaderValue("application/json");
            }

            using (sctx)
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

    }
}
