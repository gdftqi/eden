using CC.Model;
using Lilith.Core;
using Newtonsoft.Json;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading.Tasks;

namespace CC.Eva
{
    public class CreateDepart
    {
        class Request: HttpBaseRequest
        {
            [JsonProperty("name", NullValueHandling = NullValueHandling.Ignore)]
            public string? Name { get; set; }

            [JsonProperty("desc", NullValueHandling = NullValueHandling.Ignore)]
            public string? Desc { get; set; }

            [JsonProperty("user_ids", NullValueHandling = NullValueHandling.Ignore)]
            public List<Int64>? UserIDs { get; set; }
        }


        public static async Task<User> POST(string name, string desc, List<Int64>? userIDs)
        {
            if (!HttpSession.Instance.Valid)
            {
                throw new Exception("用户未登录");
            }

            var req = new Request
            {
                Name = name,
                Desc = desc,
                UserIDs = userIDs
            };

            var sw = Stopwatch.StartNew();
            return await HttpSession.Instance.PostSecureAsync<User>("/create_depart", req, true);
        }
    }
}
