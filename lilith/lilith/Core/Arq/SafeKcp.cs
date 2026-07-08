namespace Lilith.Core.Arq
{
    public class SafeKcp
    {// 线程安全的 kcp
        readonly Kcp kcp;
        readonly object locker = new object();

        public SafeKcp(Kcp kcp)
        {
            this.kcp = kcp;
        }

        public void Input(byte[] data, int offset, int len)
        {
            lock (locker)
            {
                kcp.Input(data, offset, len);
            }
        }

        public int Receive(byte[] buffer)
        {
            lock (locker)
            {
                return kcp.Receive(buffer, buffer.Length);
            }
        }

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

        // [typhon] 透传 KCP 判死信号: 0 存活 / -1 超时
        public int Update(uint currentMs)
        {
            lock (locker)
            {
                return kcp.Update(currentMs);
            }
        }

        public uint Check(uint currentMs)
        {
            lock (locker)
            {
                return kcp.Check(currentMs);
            }
        }

        // [typhon] state 是 volatile, 读写无需锁(可见性已由 volatile 保证, 同原 authed)
        public KcpState State => kcp.State;

        public void Open() => kcp.Open();
    }
}
