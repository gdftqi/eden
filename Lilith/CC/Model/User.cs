using Newtonsoft.Json;
using System;
using System.Collections.Generic;

namespace CC.Model
{
    public class User
    {
        [JsonProperty("id", NullValueHandling = NullValueHandling.Ignore)]
        public Int64? ID { get; set; }

        [JsonProperty("avatar", NullValueHandling = NullValueHandling.Ignore)]
        public string? Avatar { get; set; }

        [JsonProperty("username", NullValueHandling = NullValueHandling.Ignore)]
        public string? Username { get; set; }

        [JsonProperty("nickname", NullValueHandling = NullValueHandling.Ignore)]
        public string? Nickname { get; set; }

        [JsonProperty("phone_num", NullValueHandling = NullValueHandling.Ignore)]
        public string? PhoneNum { get; set; }

        [JsonProperty("create_time", NullValueHandling = NullValueHandling.Ignore)]
        public Int64? CreateTime { get; set; }

        [JsonProperty("state", NullValueHandling = NullValueHandling.Ignore)]
        public Int64? State { get; set; }

        [JsonProperty("depart_list", NullValueHandling = NullValueHandling.Ignore)]
        public List<Department>? DepartList { get; set; }

        public override string ToString()
        {
            return JsonConvert.SerializeObject(this);
        }
    }
}
