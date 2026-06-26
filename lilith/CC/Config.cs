using lilith.Tools;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace CC
{
    internal class Config
    {
        public const string HTTP_X25519_PK = "LZBT82+6Hdzz/pqnOyk3tRh1460vhxVJ1NcvLT3kn0M=";
        public const string HTTP_HOST = "http://172.26.29.158:8080";

        public static byte[] HttpPk = new byte[32];
        public static byte[] HttpSk = new byte[32];

        public static void Init()
        {
            Crypto.X25519KeyGen(out Config.HttpPk, out Config.HttpSk);
        }
    }
}
