using kcp2k;
using UnityEngine;


namespace Echidna
{
    public class KcpSession
    {
        static KcpSession instance;
        static KcpSession Instance
        {
            get
            {
                if (instance == null)
                {
                    instance = new KcpSession();
                }
                return instance;
            }
        }

        private KcpSession() { }


        void Connect(string host)
        {

        }

        private Kcp client;
    }
}
