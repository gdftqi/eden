using Echidna;
using kcp2k;
using lilith.Utils;
using System;
using System.Net;
using System.Net.Sockets;
using System.Threading;


namespace lilith.Core
{
    public interface ISessionEvent
    {// 会话事件
        void OnConnected(EndPoint host);
        void OnDisconnected(EndPoint host);
        void OnPackage(Package pkg);
    }


    /// <summary>
    /// KCP 客户端会话 —— 三线程模型:
    ///   主线程 : Connect/Close/Poll/Send。Poll 派发事件(回调在此触发), Send 投递(拷贝)发送包。
    ///   ioRecv : 阻塞收 UDP → 校验 MAC → safeKcp.Input → 取完整消息 → 解密/解包 →
    ///            PONG/REGIST_RSP 自己消化, 其余投 recvQue 给主线程。
    ///   ioSend : 从 sendQue 取包 → 加密 → safeKcp.SendFlush; 再 safeKcp.Update;
    ///            用 safeKcp.Check 算出下次该醒的时刻, sendQue.Wait 睡过去(空闲省电)。
    ///
    /// kcp 被 recv/send 两线程共用, 全部经 SafeKcp 串行(收 ACK 会改发送缓冲, 必须串)。
    /// 跨线程协议状态: authed 用 volatile 发布(先写 key 再置 authed, send 读到 true 即可见 key)。
    /// </summary>
    public class KcpSession
    {
        const int UDP_MTU = 1400;
        const int TICK_INTERVAL_MS = 20;   // kcp interval; 也是 send 线程空闲时最长睡多久

        static KcpSession instance = new KcpSession();

        public static KcpSession Instance
        {// 单例
            get { return instance; }
        }

        private KcpSession() { }

        public bool Running
        {
            get { return Interlocked.CompareExchange(ref running, 0, 0) == 1; }
        }

        public void SetEvent(ISessionEvent ev)
        {
            if (ev == null)
            {
                throw new Exception("ev is invalid");
            }
            this.ev = ev;
        }

        public void Connect(uint conv, string host)
        {
            if (ev == null)
            {
                throw new Exception("SessionEvent is invalid");
            }

            if (string.IsNullOrEmpty(host) || conv == 0)
            {
                throw new Exception("conv or host is null or empty");
            }

            if (Interlocked.CompareExchange(ref running, 1, 0) != 0)
            {
                return;
            }

            var ipStr = host.Substring(0, host.IndexOf(':'));
            var portStr = host.Substring(host.LastIndexOf(":") + 1);
            if (!int.TryParse(portStr, out int port))
            {
                throw new Exception("host is invalid");
            }

            try
            {
                this.conv = conv;
                this.token = Crypto.Token(conv);
                authed = false;
                sndSeq = rcvSeq = 0;

                remotePoint = new IPEndPoint(IPAddress.Parse(ipStr), port);
                sock = new Socket(AddressFamily.InterNetwork, SocketType.Dgram, ProtocolType.Udp);

                var kcp = new Kcp(conv, output);
                kcp.SetNoDelay(1, TICK_INTERVAL_MS, 3, true);
                kcp.SetMtu(UDP_MTU - Crypto.ENVELOPE_MAC_LEN);
                safeKcp = new SafeKcp(kcp);

                // 状态都在 Start 前写好, Thread.Start 自带屏障 → 两个 io 线程可见
                recvThread = new Thread(recvLoop) { IsBackground = true };
                sendThread = new Thread(sendLoop) { IsBackground = true };
                recvThread.Start();
                sendThread.Start();
            }
            catch
            {
                running = 0;
                sock?.Close();
                throw;
            }
        }

        public void Close()
        {
            if (Interlocked.CompareExchange(ref running, 0, 1) == 1)
            {
                sock?.Close();
                sendQue.Signal();
            }
        }

        public void Poll()
        {
            if (!Running)
            {
                if (recvThread != null || sendThread != null)
                {
                    recvThread?.Join();
                    sendThread?.Join();
                    recvThread = null;
                    sendThread = null;
                    ev?.OnDisconnected(remotePoint!);
                    remotePoint = null;
                    sock = null;
                    safeKcp = null;
                    authed = false;
                    sendQue.Clear();
                    recvQue.Clear();
                    Package.Pool.Clear();
                }
                return;
            }

            while (recvQue.TryDequeue(out IoEvent e))
            {
                switch (e.Kind)
                {
                    case IoEventKind.Connected:
                        ev?.OnConnected(remotePoint!);
                        break;

                    case IoEventKind.Data:
                        ev?.OnPackage(e.Pkg!);
                        Package.Pool.Return(e.Pkg!);   // 回调用完即回收
                        break;
                }
            }
        }

        // 发送: 深拷贝成 IO 独占副本投入 sendQue; seq/加密/kcp.Send 全在 send 线程做。
        // 调用方的 pkg 返回后即可自由复用/归还。
        public void Send(Package pkg)
        {
            if (!Running)
            {
                throw new Exception("Kcp session is not running");
            }

            var copy = Package.Pool.Take();
            copy.CopyFrom(pkg);
            sendQue.Enqueue(copy);
        }


        // =====================================================================
        //                            ioRecv 线程
        // =====================================================================

        private void recvLoop()
        {
            EndPoint remote = new IPEndPoint(IPAddress.Any, 0);   // ReceiveFrom 的 sender 模板, 必须非 null
            var recvBuf = new byte[UDP_MTU];

            try
            {
                while (Running)
                {
                    // 阻塞收一个 UDP 包(Close 关 socket 时这里抛 ObjectDisposed → 退出)
                    int n = sock!.ReceiveFrom(recvBuf, UDP_MTU, SocketFlags.None, ref remote);
                    if (n < Crypto.ENVELOPE_MAC_LEN + Crypto.ENVELOPE_MAC_HASH_LEN)
                    {
                        continue;
                    }

                    var tag = Crypto.SipHashTag(recvBuf, Crypto.ENVELOPE_MAC_LEN, Crypto.ENVELOPE_MAC_HASH_LEN);
                    if (!MacMatch(tag, recvBuf))
                    {
                        continue;
                    }

                    safeKcp!.Input(recvBuf, Crypto.ENVELOPE_MAC_LEN, n - Crypto.ENVELOPE_MAC_LEN);

                    while (true)
                    {
                        int m = safeKcp!.Receive(rbuf);
                        if (m <= 0)
                        {
                            break;
                        }
                        onMessage(m);
                    }
                }
            }
            catch (ObjectDisposedException) { }     // Close 关了 socket
            catch (SocketException) { }             // 网络错误
            catch { /* TODO: log; 单包错误也走到这, 直接收尾 */ }
            finally { markDead(); }
        }

        // 一条完整 KCP 消息: rbuf[0..n) = 明文 10B 头 + (密文 payload + 16B tag) 或明文 payload。
        // 仅 ioRecv 线程调用。PONG/REGIST_RSP 就地消化, 其余投 recvQue 给主线程。
        private void onMessage(int n)
        {
            if (n < Package.HEADER_SIZE)
            {
                return;
            }

            Package.Decode32BE(rbuf, Package.OFFSET_SEQ, out uint seq);   // 头永远明文, 解出 seq 当 nonce

            if (!authed)
            {
                // 期望 REGIST_RSP(明文): payload = 服务端临时 X25519 公钥(32B)
                if (!Package.Unpack(rbuf, n, rcvPkg))
                {
                    return;
                }

                if (rcvPkg.PkId == Package.PKID_REGIST_RSP && rcvPkg.PayloadLength >= 32)
                {
                    var srvPk = new byte[32];
                    Buffer.BlockCopy(rcvPkg.Payload, 0, srvPk, 0, 32);
                    Crypto.KxClient(srvPk, out rxKey, out txKey);   // 先派生 key...
                    authed = true;                                  // ...再 volatile 置位: 把 key 发布给 send 线程
                    recvQue.Enqueue(new IoEvent(IoEventKind.Connected, null));
                }
                return;
            }

            // authed 业务包: 有 payload = 头 + 密文 + tag, 需解密
            int plen = n - Package.HEADER_SIZE;
            if (plen > 0)
            {
                if (plen < Crypto.XX20_TAG_LEN)
                {
                    return;     // 连 tag 都放不下
                }

                var body = new byte[plen];
                Buffer.BlockCopy(rbuf, Package.HEADER_SIZE, body, 0, plen);
                var nonce = Crypto.MakeNonce(conv, seq, Crypto.DIR_S2C);
                var plain = Crypto.Decrypt(rxKey!, nonce, body, plen);
                if (plain == null)
                {
                    return;     // 验签失败, 丢弃
                }

                Buffer.BlockCopy(plain, 0, rbuf, Package.HEADER_SIZE, plain.Length);
                n = Package.HEADER_SIZE + plain.Length;
            }

            var pkg = Package.Pool.Take();
            if (!Package.Unpack(rbuf, n, pkg))
            {
                Package.Pool.Return(pkg);
                return;
            }

            if (pkg.PkSeq <= rcvSeq)        // 幂等去重
            {
                Package.Pool.Return(pkg);
                return;
            }
            rcvSeq = pkg.PkSeq;

            // TODO: PONG 等传输层保活消息在此就地消费(协议号未定义; 定义后:
            //       if (pkg.PkId == Package.PKID_PONG) { /* 更新存活时间 */ Package.Pool.Return(pkg); return; })

            recvQue.Enqueue(new IoEvent(IoEventKind.Data, pkg));
        }


        // =====================================================================
        //                            ioSend 线程
        // =====================================================================

        private void sendLoop()
        {
            try
            {
                registReq();   // 握手第一包由持有 kcp 的 send 线程发(authed=false → 不加密)

                while (Running)
                {
                    while (sendQue.TryDequeue(out Package pkg))
                    {
                        doSend(pkg);
                        Package.Pool.Return(pkg);
                    }

                    uint now = (uint)Environment.TickCount;
                    safeKcp!.Update(now);

                    int wait = (int)(safeKcp!.Check(now) - now);   // 下次该 flush 的时刻
                    if (wait < 0) wait = 0;
                    if (wait > TICK_INTERVAL_MS) wait = TICK_INTERVAL_MS;
                    sendQue.Wait(wait);   // 睡到: 有人 Enqueue / Close Signal / 超时
                }
            }
            catch (ObjectDisposedException) { }
            catch (SocketException) { }
            catch { /* TODO: log */ }
            finally { markDead(); }
        }

        // stamp seq + (authed 时)加密 payload + SendFlush。仅 ioSend 线程。
        private void doSend(Package pkg)
        {
            pkg.PkSeq = ++sndSeq;
            int total = Package.Pack(pkg, pkSendBuf);

            // authed 且有 payload → 原地把 payload 换成 密文 + 16B tag
            if (authed && pkg.PayloadLength > 0)
            {
                var nonce = Crypto.MakeNonce(conv, pkg.PkSeq, Crypto.DIR_C2S);
                var enc = Crypto.Encrypt(txKey!, nonce, pkg.Payload, pkg.PayloadLength);
                Buffer.BlockCopy(enc, 0, pkSendBuf, Package.HEADER_SIZE, enc.Length);
                total = Package.HEADER_SIZE + enc.Length;
            }

            if (safeKcp!.SendFlush(pkSendBuf, 0, total) != 0)
            {
                throw new Exception("Kcp.Send failed");
            }
        }

        // 握手第一包: REGIST_REQ, dst_id = GATEWAY_ID, payload = 明文 token。仅 ioSend 线程。
        private void registReq()
        {
            var pkg = Package.Pool.Take();
            pkg.PkId = Package.PKID_REGIST_REQ;
            pkg.PkDstId = Package.GATEWAY_ID;
            Buffer.BlockCopy(token, 0, pkg.Payload, 0, token!.Length);
            pkg.PayloadLength = token.Length;
            doSend(pkg);
            Package.Pool.Return(pkg);
        }


        // =====================================================================

        // KCP output 回调: prepend 8B envelope MAC(SipHash 覆盖 frame 前 24B), 再 UDP 发出。
        // 由 send 线程在 SafeKcp 锁内触发(SendFlush/Update), 故 udpSendBuf 单线程独占。
        private void output(byte[] segment, int size)
        {
            var mac = Crypto.SipHashTag(segment, Math.Min(size, Crypto.ENVELOPE_MAC_HASH_LEN));
            Buffer.BlockCopy(mac, 0, udpSendBuf, 0, Crypto.ENVELOPE_MAC_LEN);
            Buffer.BlockCopy(segment, 0, udpSendBuf, Crypto.ENVELOPE_MAC_LEN, size);
            size += Crypto.ENVELOPE_MAC_LEN;
            sock!.SendTo(udpSendBuf, size, SocketFlags.None, remotePoint!);
        }

        // tag(8B) 是否等于 data 开头的 8B envelope MAC
        private static bool MacMatch(byte[] tag, byte[] data)
        {
            for (int i = 0; i < Crypto.ENVELOPE_MAC_LEN; i++)
                if (tag[i] != data[i]) return false;
            return true;
        }


        // ---- IO → 主线程 的事件 ----
        private enum IoEventKind : byte { Connected, Data }

        private readonly struct IoEvent
        {
            public readonly IoEventKind Kind;
            public readonly Package? Pkg;     // 仅 Data 有效
            public IoEvent(IoEventKind kind, Package? pkg)
            {
                Kind = kind;
                Pkg = pkg;
            }
        }


        // ---- 跨线程队列(自带锁 + 条件变量, 非 ConcurrentQueue) ----
        private readonly BlockingQueue<Package> sendQue = new BlockingQueue<Package>();   // 主 → send
        private readonly BlockingQueue<IoEvent> recvQue = new BlockingQueue<IoEvent>();   // recv → 主

        // ---- 控制 / 共享 ----
        private int running = 0;
        private Thread? recvThread = null;
        private Thread? sendThread = null;
        private ISessionEvent? ev = null;          // 回调仅主线程触发
        private EndPoint? remotePoint = null;       // Connect 设(Start 前) / cleanup 清(Join 后)
        private Socket? sock = null;                 // Connect 建; recv 收 / send 发(同一 UDP socket 全双工安全); Close 关
        private SafeKcp? safeKcp = null;             // recv/send 共用, 内部一把锁串行
        private volatile bool authed = false;        // 跨线程: recv 握手时写, send 加密时读 → volatile 发布 key

        // ---- Connect 前发布, 之后只读 ----
        private uint conv = 0;
        private byte[] token = new byte[170];

        // ---- 仅 ioRecv 线程 ----
        private byte[] rxKey = new byte[32];
        private uint rcvSeq = 0;
        private byte[] rbuf = new byte[Package.PACK_MAX_LEN + 1];   // kcp.Receive 暂存
        private Package rcvPkg = new Package();                     // 握手包解析暂存(不入队)

        // ---- 仅 ioSend 线程(txKey 例外: recv 握手时写入, 经 authed 发布后 send 读) ----
        private byte[] txKey = new byte[32];
        private uint sndSeq = 0;
        private byte[] pkSendBuf = new byte[Package.PACK_MAX_LEN];  // pack/加密暂存
        private byte[] udpSendBuf = new byte[Package.PACK_MAX_LEN + Crypto.ENVELOPE_MAC_LEN]; // output 暂存
    }
}
