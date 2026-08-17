using System.Threading.Tasks;

namespace Lilith.Core.Eva
{
    internal class Ping
    {
        public static async Task<int> POST()
        {
            var http = HttpSession.Instance;
            if (!http.Valid)
            {
                return -1;
            }

            var body = new HttpReq
            {
                UserID = http.UserID!.Value,
                Data   = http.Encrypt(new HttpBaseRequest()),
            };

            var rsp = await http.PostAynsc("/update", body);
            return rsp.Code;
        }
    }
}
