using CC.Model;
using Lilith.Core;
using Newtonsoft.Json;
using System;
using System.Collections.Generic;
using System.Threading.Tasks;

namespace CC.Eva
{
    public class GetOrg
    {
        class Request : HttpBaseRequest
        {
        }

        public class Response
        {
            [JsonProperty("departs", NullValueHandling = NullValueHandling.Ignore)]
            public List<Department>? Departs { get; set; }

            [JsonProperty("users", NullValueHandling = NullValueHandling.Ignore)]
            public List<User>? Users { get; set; }
        }


        public static async Task<Response> POST()
        {
            if (!HttpSession.Instance.Valid)
            {
                throw new Exception("用户未登录");
            }

            return await HttpSession.Instance.PostSecureAsync<Response>("/get_org", new Request(), true);
        }
    }
}
