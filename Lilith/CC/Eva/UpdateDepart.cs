using CC.Model;
using Lilith.Core;
using Newtonsoft.Json;
using System;
using System.Collections.Generic;
using System.Threading.Tasks;

namespace CC.Eva
{
    public class UpdateDepart
    {
        // avatar/desc 可空 + Ignore: 不传就是"这次不改", 传空串是"清空".
        // 服务端那边对应的是 *string.
        // name 不一样, 服务端收的是 string, 判的是 len(Name) > 0 -- 空 = 不改, 清不掉
        class Request : HttpBaseRequest
        {
            [JsonProperty("depart_id")]
            public Int64 DepartID { get; set; }

            [JsonProperty("name", NullValueHandling = NullValueHandling.Ignore)]
            public string? Name { get; set; }

            [JsonProperty("avatar", NullValueHandling = NullValueHandling.Ignore)]
            public string? Avatar { get; set; }

            [JsonProperty("desc", NullValueHandling = NullValueHandling.Ignore)]
            public string? Desc { get; set; }

            [JsonProperty("add_user_ids", NullValueHandling = NullValueHandling.Ignore)]
            public List<Int64>? AddUserIDs { get; set; }

            [JsonProperty("del_user_ids", NullValueHandling = NullValueHandling.Ignore)]
            public List<Int64>? DelUserIDs { get; set; }
        }

        /// <summary>
        /// 改部门. 每一项传 null 表示这次不改.
        /// </summary>
        /// <returns>服务端改完之后的部门(增删成员之后重新查的, 不是本地拼的)</returns>
        public static async Task<Department> POST(Int64 departID,
                                                  string? name = null,
                                                  string? avatar = null,
                                                  string? desc = null,
                                                  List<Int64>? addUserIDs = null,
                                                  List<Int64>? delUserIDs = null)
        {
            if (!HttpSession.Instance.Valid)
            {
                throw new Exception("用户未登录");
            }

            var req = new Request
            {
                DepartID = departID,
                Name = name,
                Avatar = avatar,
                Desc = desc,
                AddUserIDs = addUserIDs,
                DelUserIDs = delUserIDs,
            };

            // 服务端 updateDepartRsp 内嵌的是 *dao.Department, JSON 是摊平的, 没有外面那一层
            return await HttpSession.Instance.PostSecureAsync<Department>("/update_depart", req, true);
        }
    }
}
