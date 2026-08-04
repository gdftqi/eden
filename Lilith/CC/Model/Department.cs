using Newtonsoft.Json;
using System;

namespace CC.Model
{
    public class Department
    {
        [JsonProperty("id", NullValueHandling = NullValueHandling.Ignore)]
        public Int64? ID { get; set; }

        [JsonProperty("name", NullValueHandling = NullValueHandling.Ignore)]
        public string? Name { get; set; }

        [JsonProperty("desc", NullValueHandling = NullValueHandling.Ignore)]
        public string? Desc { get; set; }

        [JsonProperty("state", NullValueHandling = NullValueHandling.Ignore)]
        public Int64? State { get; set; }

        public override string ToString()
        {
            return JsonConvert.SerializeObject(this);
        }
    }
}
