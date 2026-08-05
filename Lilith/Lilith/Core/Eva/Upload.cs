using Newtonsoft.Json;
using System;
using System.IO;
using System.Net.Http;
using System.Threading;
using System.Threading.Tasks;

namespace Lilith.Core.Eva
{
    public class Upload
    {
        public const string Url = "/upload";
        public const long MaxSize = 200 << 20;

        class Request : HttpBaseRequest
        {
            [JsonProperty("name", NullValueHandling = NullValueHandling.Ignore)]
            public string? Name { get; set; }

            [JsonProperty("size", NullValueHandling = NullValueHandling.Ignore)]
            public Int64 Size { get; set; }
        }

        class Response
        {
            [JsonProperty("url", NullValueHandling = NullValueHandling.Ignore)]
            public string? Url { get; set; }
        }


        public static async Task<string> POST(string filePath, CancellationToken cancellationToken = default)
        {
            var fi = new FileInfo(filePath);
            if (!fi.Exists)
            {
                throw new FileNotFoundException("文件不存在", filePath);
            }

            using var fs = fi.OpenRead();
            return await POST(fs, fi.Name, fi.Length, cancellationToken);
        }


        public static async Task<string> POST(Stream content, string name, long size, CancellationToken cancellationToken = default)
        {
            if (!HttpSession.Instance.Valid)
            {
                throw new InvalidOperationException("用户未登录");
            }

            if (size <= 0 || size > MaxSize)
            {
                throw new InvalidOperationException("文件大小超出限制");
            }

            var req = new Request
            {
                Name = name,
                Size = size,
            };

            using var form = new MultipartFormDataContent();

            form.Add(new StringContent(HttpSession.Instance.UserID!.Value.ToString()), "user_id");
            form.Add(new StringContent(HttpSession.Instance.Encrypt(req)), "data");

            var file = new StreamContent(content);
            form.Add(file, "file", name);

            var rsp = await HttpSession.Instance.PostFormAsync<Response>(Url, form, cancellationToken);
            return rsp.Url ?? string.Empty;
        }
    }
}
