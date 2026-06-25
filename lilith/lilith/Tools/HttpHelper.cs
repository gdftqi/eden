using System;
using System.Collections.Generic;
using System.Text;

namespace lilith.Tools
{
    class HttpHelper
    {
        private static HttpHelper instance = new HttpHelper();
        public static HttpHelper Instance
        {
            get
            {
                return instance;
            }
        }

        private HttpHelper()
        { }
    }
}
