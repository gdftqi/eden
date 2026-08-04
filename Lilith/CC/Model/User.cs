using Newtonsoft.Json;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace CC.Model
{
    public class User
    {
        [JsonProperty("id", NullValueHandling = NullValueHandling.Ignore)]
        public string? ID { get; set; }

        [JsonProperty("Nickname", NullValueHandling = NullValueHandling.Ignore)]
        public string? Nickname { get; set; }

        [JsonProperty("PhoneNum", NullValueHandling = NullValueHandling.Ignore)]
        public string? PhoneNum { get; set; }

        [JsonProperty("username", NullValueHandling = NullValueHandling.Ignore)]
        public string? Username { get; set; }

        [JsonProperty("create_time", NullValueHandling = NullValueHandling.Ignore)]
        public Int64? CreateTime { get; set; }

        [JsonProperty("state", NullValueHandling = NullValueHandling.Ignore)]
        public Int64? State { get; set; }

        [JsonProperty("departments", NullValueHandling = NullValueHandling.Ignore)]
        public IList<Department>? Departs { get; set; }

        public override string ToString()
        {
            return JsonConvert.SerializeObject(this);
        }
    }
}
