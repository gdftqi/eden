using CC.Model;
using Lilith.Core;
using Newtonsoft.Json;
using System;
using System.Diagnostics;
using System.Threading.Tasks;

namespace CC.Eva
{
    public class CreateUser
    {
        class Request : HttpBaseRequest
        {
            [JsonProperty("username", NullValueHandling = NullValueHandling.Ignore)]
            public string? Username { get; set; }

            [JsonProperty("password", NullValueHandling = NullValueHandling.Ignore)]
            public string? Password { get; set; }

            [JsonProperty("nickname", NullValueHandling = NullValueHandling.Ignore)]
            public string? Nickname { get; set; }

            [JsonProperty("phone_num", NullValueHandling = NullValueHandling.Ignore)]
            public string? PhoneNum { get; set; }
        }


        class Response
        {

        }

        public static async Task<User> POST(string username, string password, string nickname, string phoneNum)
        {
            if (!HttpSession.Instance.Valid)
            {
                throw new Exception("用户未登录");
            }

            var req = new Request
            {
                Username = username,
                Password = password,
                Nickname = nickname,
                PhoneNum = phoneNum
            };

            var sw = Stopwatch.StartNew();
            return await HttpSession.Instance.PostSecureAsync<User>("/create_user", req, true); 
        }
    }
}
