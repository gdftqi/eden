// Kcp 的线程安全包装 —— 一把锁串行化所有 kcp 调用。
using kcp2k;


namespace lilith.Utils
{
    /// <summary>
    /// Kcp 的线程安全壳。ikcp 的收/发路径**共享同一批内部状态**(snd_buf / acklist /
    /// cwnd —— 收 ACK 会改发送缓冲), 所以 Input(收) 与 Send/Flush(发) 必须串行,
    /// 否则两线程同时改一条链表会损坏状态机。
    ///
    /// 用法: 所有对 kcp 的访问都走这层, 就不可能漏锁。构造时传入一个已经
    /// new + SetNoDelay + SetMtu 好的 Kcp(output 回调是会话特有的, 壳不掺和)。
    ///
    /// 注意: kcp.Send/Flush/Update 会**同步回调 output**(在锁内), 即 SipHash + SendTo
    /// 也在持锁期间执行。对低速客户端可接受(SendTo 不阻塞)。
    /// </summary>
    public class SafeKcp
    {
        readonly Kcp kcp;
        readonly object locker = new object();

        public SafeKcp(Kcp kcp)
        {
            this.kcp = kcp;
        }

        // 喂入一帧(已剥掉 envelope MAC 的 KCP frame)。recv 线程调用。
        public void Input(byte[] data, int offset, int len)
        {
            lock (locker)
            {
                kcp.Input(data, offset, len);
            }
        }

        // 取一条完整消息到 buffer; 返回字节数, <=0 表示暂无。recv 线程循环调用。
        public int Receive(byte[] buffer)
        {
            lock (locker)
            {
                return kcp.Receive(buffer, buffer.Length);
            }
        }

        // 发送 + 立即 flush 作为一个原子段(避免两线程在 Send 和 Flush 之间插入)。
        // 返回 kcp.Send 的结果(0 = 成功)。send 线程调用。
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

        // 驱动一次(重传 / ACK / flush)。send 线程周期调用。
        public void Update(uint currentMs)
        {
            lock (locker)
            {
                kcp.Update(currentMs);
            }
        }

        // 算出下次该 flush 的时刻(毫秒时钟), 拿来当 send 线程条件等待的超时。
        public uint Check(uint currentMs)
        {
            lock (locker)
            {
                return kcp.Check(currentMs);
            }
        }
    }
}
