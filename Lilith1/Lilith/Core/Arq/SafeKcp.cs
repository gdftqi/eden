namespace Lilith.Core.Arq
{
    /// <summary>
    /// 线程安全的 KCP
    /// </summary>
    public class SafeKcp
    {
        /// <summary>
        /// 构造函数
        /// </summary>
        /// <param name="kcp"></param>
        public SafeKcp(Kcp kcp)
        {
            this.kcp = kcp;
        }


        /// <summary>
        /// 状态
        /// </summary>
        public KcpState State
        {
            get
            {
                return kcp.State;
            }

            set
            {
                kcp.state = value;
            }
        }


        /// <summary>
        /// KCP 状态 鉴权成功变为 OPEN
        /// </summary>
        public void Open()
        {
            lock (locker)
            {
                kcp.Open();
            }
        }


        /// <summary>
        /// Kcp Input
        /// </summary>
        /// <param name="data"></param>
        /// <param name="offset"></param>
        /// <param name="len"></param>
        public void Input(byte[] data, int offset, int len)
        {
            lock (locker)
            {
                kcp.Input(data, offset, len);
            }
        }


        /// <summary>
        /// Kcp Recv
        /// </summary>
        /// <param name="buffer"></param>
        /// <returns></returns>
        public int Receive(byte[] buffer)
        {
            lock (locker)
            {
                return kcp.Receive(buffer, buffer.Length);
            }
        }


        /// <summary>
        /// Kcp Send + Kcp Flush
        /// </summary>
        /// <param name="buffer"></param>
        /// <param name="offset"></param>
        /// <param name="len"></param>
        /// <returns></returns>
        public int SendFlush(byte[] buffer, int offset, int len)
        {
            lock (locker)
            {
                int r = kcp.Send(buffer, offset, len);
                if (r == 0)
                {
                    kcp.Flush();
                }
                return r;
            }
        }


        /// <summary>
        /// Kcp Update
        /// </summary>
        /// <param name="currentMs"></param>
        /// <returns></returns>
        public int Update(uint currentMs)
        {
            lock (locker)
            {
                return kcp.Update(currentMs);
            }
        }


        /// <summary>
        /// 获取 KCP 下一次的 Update 时间
        /// </summary>
        /// <param name="currentMs"></param>
        /// <returns></returns>
        public uint Check(uint currentMs)
        {
            lock (locker)
            {
                return kcp.Check(currentMs);
            }
        }


        // kcp 对象
        readonly Kcp kcp;

        // 锁
        readonly object locker = new object();
    }
}
