using Lilith.Core.Arq;
using Lilith.Utils;
using System;
using System.Net;
using System.Net.Sockets;
using System.Runtime.CompilerServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;


namespace Lilith.Core
{
    public interface ISessionEvent
    {// 会话事件
        void OnConnected(EndPoint host);
        void OnDisconnected(EndPoint host);
        void OnPackage(Package pkg);
    }

    enum IOEventType : byte
    {// IO 事件 类型
        Connected, // 连接
        Disconnected, // 连接断开
        RcvData // 接收数据
    }

    class IOEvent
    {// IO 事件
        public IOEventType Type;
        public Package? Pkg = null;
        public string? Arg = null;

        public IOEvent(IOEventType type)
        {
            Type = type;
        }

        public IOEvent(IOEventType type, Package pkg)
        {
            Type = type;
            Pkg = pkg;
        }
    }

    public enum DisconnectReason : byte
    {// 断线原因
        None,     // 正常 / 主动关闭 / 未知
        Timeout,  // 超时失联(可重试)
        Rst,      // 服务端 RST: 会话已不存在(直接走 /refresh 重登)
        Reset     // socket 层强制断开/不可达(如 10054): 服务端崩溃或重启, 需重连
    }

    public class KcpSession
    {// Kcp 会话
        const int UDP_MTU = 1450;   // 必须与服务端 core::typhon.in.hpp 的 UDP_MTU 一致
        const int TICK_INTERVAL_MS = 10;
        const int SEND_BATCH = 128;
        const int RECV_BATCH = 128;

        // 握手超时: Connect 后超过此时长仍未 Open(握手完成)就判死。
        // 独立于 KCP 的 30s 失联超时 —— 撞上网关旧会话时传输层 PING/PONG 互相保活,
        // last_rcv_ms 一直被刷新, KCP 的超时判死永不触发, 但 REGIST_RSP 永远不会来。
        const uint HANDSHAKE_TIMEOUT_MS = 5000;

        static KcpSession instance = new KcpSession();

        public static KcpSession Instance
        {// 单例
            get { return instance; }
        }

        private KcpSession()
        {
            Crypto.X25519KeyGen(out pk, out sk);
        }

        public uint GatewayID
        {// 网关ID
            get; set;
        }

        public bool Running
        {// 是否运行中
            get
            {
                return Interlocked.CompareExchange(ref running, 0, 0) == 1;
            }
        }

        public byte[] PK { get { return pk; } }
        public byte[] SK { get { return sk; } }

        // [typhon] 最近一次断线原因; OnDisconnected 里读它选重连策略(Rst→/refresh, Timeout→可重试)
        public DisconnectReason DeadReason { get; private set; }

        // [typhon] 本次连接的握手结果: Connect 返回它; 首个 Connected → true, 断开 → false
        private TaskCompletionSource<bool>? connectTsk;

        public void SetEvent(ISessionEvent ev)
        {
            this.ev = ev;
        }

        public void Init(string host, uint conv, uint userId, uint gatewayId, string macKey, string b64Token)
        {
            this.host = host;
            this.conv = conv;
            this.userId = userId;
            token = Crypto.Base64DecodeToBytes(b64Token);
            DeadReason = DisconnectReason.None;
            sndSeq = rcvSeq = 0;
            GatewayID = gatewayId;
            this.macKey = Encoding.ASCII.GetBytes(macKey);
        }


        public Task<bool> Connect(uint timeout)
        {// 连接服务
            FileLog.Write($"[KCP] Connect() 调用, running(调用前)={running}");

            if (ev == null)
            {// 入参检查
                throw new Exception("param is invalid");
            }

            if (Interlocked.CompareExchange(ref running, 1, 0) != 0)
            {// 已在运行, 返回当前这次连接的结果
                FileLog.Write($"[KCP] Connect() 命中'已在运行'分支, connectTsk是否为null={connectTsk == null}, 该Task是否已完成={connectTsk?.Task.IsCompleted}");
                return connectTsk?.Task ?? Task.FromResult(false);
            }

            FileLog.Write($"[KCP] Connect() CAS 成功, 新建 connectTsk, host={host}, conv={conv}");
            connectTsk = new TaskCompletionSource<bool>();

            var ipStr = host.Substring(0, host.IndexOf(':'));
            var portStr = host.Substring(host.LastIndexOf(":") + 1);
            if (!int.TryParse(portStr, out int port))
            {
                running = 0;
                throw new Exception("host is invalid");
            }

            try
            {
                remotePoint = new IPEndPoint(IPAddress.Parse(ipStr), port);
                sock = new Socket(AddressFamily.InterNetwork, SocketType.Dgram, ProtocolType.Udp);
                sock.Bind(new IPEndPoint(IPAddress.Any, 0));

                var kcp = new Kcp(conv, output);
                kcp.SetSipHash(macKey!);
                kcp.SetNoDelay(1, TICK_INTERVAL_MS, 3, true);
                kcp.SetMtu(UDP_MTU - Crypto.ENVELOPE_MAC_LEN);
                kcp.SetPing(true);
                kcp.SetTimeout(timeout);
                safeKcp = new SafeKcp(kcp);

                connectStartMs = (uint)Environment.TickCount;   // 握手超时起点
                rcvThread = new Thread(rcvLoop) { IsBackground = true };
                sndThread = new Thread(sndLoop) { IsBackground = true };
                rcvThread.Start();
                sndThread.Start();
                FileLog.Write($"[KCP] Connect() 线程已起, rcvTid={rcvThread.ManagedThreadId}, sndTid={sndThread.ManagedThreadId}");
            }
            catch (Exception ex)
            {
                FileLog.Write($"[KCP] Connect() 异常: {ex}");
                running = 0;
                sock?.Close();
                throw;
            }

            return connectTsk.Task;
        }

        public void Close()
        {// 关闭连接
            FileLog.Write($"[KCP] Close() 调用, running(调用前)={running}, DeadReason={DeadReason}");
            if (Interlocked.CompareExchange(ref running, 0, 1) == 1)
            {
                FileLog.Write($"[KCP] Close() CAS 成功, 本次由我发起 teardown");
                sock?.Close();
                sndQue.Signal();
                Notify();
            }
            else
            {
                FileLog.Write($"[KCP] Close() CAS 失败(已经是 0), 跳过");
            }
        }

        public void Update()
        {// 刷新 Package 句柄
            Interlocked.Exchange(ref notifyPending, 0);
            if (!Running)
            {
                if (rcvThread != null || sndThread != null)
                {
                    FileLog.Write($"[KCP] Update() 进入 teardown 分支, 开始 Join 线程");
                    rcvThread?.Join();
                    sndThread?.Join();
                    rcvThread = null;
                    sndThread = null;
                    FileLog.Write($"[KCP] Update() 线程已 Join 完毕, connectTsk是否为null={connectTsk == null}, TrySetResult(false) 之前是否已完成={connectTsk?.Task.IsCompleted}");
                    bool won = connectTsk?.TrySetResult(false) ?? false;
                    FileLog.Write($"[KCP] Update() TrySetResult(false) 结果={won}, 即将调用 ev.OnDisconnected, ev是否为null={ev == null}");
                    ev?.OnDisconnected(remotePoint!);
                    FileLog.Write($"[KCP] Update() OnDisconnected 返回, 清理状态完毕");
                    remotePoint = null;
                    sock = null;
                    safeKcp = null;
                    sndQue.Clear();
                    rcvQue.Clear();
                    Package.Pool.Clear();
                }
                return;
            }

            var evs = new IOEvent[RECV_BATCH];
            int n = rcvQue.Wait(evs);

            if (n > 0)
            {
                FileLog.Write($"[KCP] Update() 本轮取到 {n} 个事件: [{string.Join(",", Array.ConvertAll(evs, e => e?.Type.ToString() ?? "null"), 0, n)}]");
            }

            for (int i = 0; i < n; i++)
            {
                switch (evs[i].Type)
                {
                    case IOEventType.Connected:
                        bool tsWon = connectTsk?.TrySetResult(true) ?? false;
                        FileLog.Write($"[KCP] Update() 收到 Connected 事件, TrySetResult(true) 结果={tsWon}, 即将调用 ev.OnConnected");
                        ev?.OnConnected(remotePoint!);
                        break;

                    case IOEventType.RcvData:
                        ev?.OnPackage(evs[i].Pkg!);
                        Package.Pool.Return(evs[i].Pkg!);
                        break;
                }
            }
        }

        private void Notify()
        {
            bool fired = Interlocked.Exchange(ref notifyPending, 1) == 0;
            FileLog.Write($"[KCP] Notify() 调用, 是否实际触发 OnWakeup={fired}(false=被合并/丢弃, 说明已有一次唤醒待处理)");
            if (fired)
            {
                OnWakeup?.Invoke();
            }
        }

        public void Send(Package pkg)
        {// 发送 Package
            if (!Running)
            {
                throw new Exception("Kcp session is not running");
            }

            if (safeKcp!.State != Arq.KcpState.Open)
            {// [typhon] 握手未完成, 不允许发业务包(握手包走 registReq, 不经这里)
                return;
            }

            var copy = Package.Pool.Take();
            copy.CopyFrom(pkg);
            sndQue.Enqueue(copy);
        }

        private void rcvLoop()
        {// 接收线程
            FileLog.Write($"[KCP] rcvLoop 启动");
            EndPoint remote = new IPEndPoint(IPAddress.Any, 0);
            var recvBuf = new byte[UDP_MTU];
            int n = 0;

            try
            {
                while (Running)
                {
                    n = sock!.ReceiveFrom(recvBuf, UDP_MTU, SocketFlags.None, ref remote);
                    if (!remote.Equals(remotePoint))
                    {// 只收来自服务器的包, 其它来源丢弃
                        continue;
                    }

                    safeKcp!.Input(recvBuf, 0, n);

                    while (true)
                    {
                        n = safeKcp!.Receive(rbuf);
                        if (n <= 0)
                        {
                            break;
                        }

                        var pkg = Package.Pool.Take();
                        if (!decode(rbuf, n, pkg))
                        {
                            Package.Pool.Return(pkg);
                            continue;
                        }

                        switch (pkg.ID)
                        {
                            case Package.PKID_REGIST_RSP:
                                onRegistRsp(pkg);
                                break;

                            case Package.PKID_PONG:
                                onPong(pkg);
                                break;

                            default:
                                onDefault(pkg);
                                break;
                        }
                    }
                }
            }
            catch (ObjectDisposedException)
            {
                // Close 关了 socket, 正常收尾
            }
            catch (SocketException ex)
            {
                FileLog.Write($"[KCP] recvLoop SocketException: {ex.SocketErrorCode} ({ex.ErrorCode}) {ex.Message}");
                if (Running)
                {// 非主动关闭(Close 会先把 running 置 0)→ 标记异常断线, 触发重连
                    DeadReason = DisconnectReason.Reset;
                }
            }
            catch (Exception ex)
            {
                FileLog.Write("[KCP] recvLoop error: " + ex);
                if (Running)
                {
                    DeadReason = DisconnectReason.Reset;
                }
            }
            finally
            {
                FileLog.Write($"[KCP] rcvLoop 退出");
                Close();
            }
        }

        private void onRegistRsp(Package pkg)
        {// PKID_REGIST_RSP 句柄
            FileLog.Write($"[KCP] onRegistRsp 收到, state(收到前)={safeKcp!.State}, PayloadLength={pkg.PayloadLength}");

            if (safeKcp!.State == Arq.KcpState.Open)
            {// 已握手完成
                return;
            }

            if (pkg.PayloadLength != 32)
            {
                return;
            }

            Crypto.X25519KxClient(sk, pk, pkg.Payload, out rxKey, out txKey);
            safeKcp!.Open();   // 密钥已就绪, 置 Open(volatile 写把 rxKey/txKey 一并发布给发送线程)
            rcvQue.Enqueue(new IOEvent(IOEventType.Connected));
            FileLog.Write($"[KCP] onRegistRsp 鉴权成功, 已入队 Connected 事件, 即将 Notify()");
            Notify();
            Package.Pool.Return(pkg);
        }

        private void onPong(Package pkg)
        {// PKID_PONG 句柄
            if (safeKcp!.State != Arq.KcpState.Open)
            {
                return;
            }

            if (pkg.PayloadLength != 8)
            {
                return;
            }

            // TODO: 打印RTT
            Package.Pool.Return(pkg);
        }

        private void onDefault(Package pkg)
        {// 默认句柄
            if (safeKcp!.State != Arq.KcpState.Open || pkg.Idempotent <= rcvSeq)
            {
                Package.Pool.Return(pkg);
                return;
            }

            rcvSeq = pkg.Idempotent;
            rcvQue.Enqueue(new IOEvent(IOEventType.RcvData, pkg));
            Notify();
        }

        private void sndLoop()
        {// 发送线程
            FileLog.Write($"[KCP] sndLoop 启动");
            try
            {
                registReq();
                FileLog.Write($"[KCP] sndLoop 已发出 REGIST_REQ, GatewayID={GatewayID}");
                var pks = new Package[SEND_BATCH];
                int wait = 0;
                while (Running)
                {
                    int n = sndQue.Wait(pks, wait);
                    for (int i = 0; i < n; i++)
                    {
                        doSend(pks[i]);
                        Package.Pool.Return(pks[i]);
                    }

                    uint tnow = (uint)Environment.TickCount;
                    int dead = safeKcp!.Update(tnow);
                    if (dead < 0)
                    {
                        DeadReason = dead == (int)Arq.KcpState.Rst ? DisconnectReason.Rst : DisconnectReason.Timeout;
                        FileLog.Write($"[KCP] sndLoop 判死, dead={dead}, DeadReason={DeadReason}");
                        Close();
                        break;
                    }

                    if (safeKcp!.State != Arq.KcpState.Open && tnow - connectStartMs > HANDSHAKE_TIMEOUT_MS)
                    {// 握手超时: 传输层可能还活着(PING/PONG), 但 REGIST_RSP 不来, 必须独立判死
                        DeadReason = DisconnectReason.Timeout;
                        FileLog.Write($"[KCP] sndLoop 握手超时({HANDSHAKE_TIMEOUT_MS}ms 未 Open), 判死");
                        Close();
                        break;
                    }

                    wait = (int)(safeKcp!.Check(tnow) - tnow);
                    if (wait < 0 || n == pks.Length)
                    {
                        wait = 0;
                    }
                    else if (wait > TICK_INTERVAL_MS)
                    {
                        wait = TICK_INTERVAL_MS;
                    }
                }
            }
            catch (ObjectDisposedException)
            {
                // Close 关了 socket, 正常收尾
            }
            catch (SocketException ex)
            {
                FileLog.Write($"[KCP] sendLoop SocketException: {ex.SocketErrorCode} ({ex.ErrorCode}) {ex.Message}");
                if (Running)
                {// 非主动关闭 → 标记异常断线, 触发重连
                    DeadReason = DisconnectReason.Reset;
                }
            }
            catch (Exception ex)
            {
                FileLog.Write("[KCP] sendLoop error: " + ex);
                if (Running)
                {
                    DeadReason = DisconnectReason.Reset;
                }
            }
            finally
            {
                FileLog.Write($"[KCP] sndLoop 退出");
                Close();
            }
        }

        private void doSend(Package pkg)
        {
            pkg.Idempotent = ++sndSeq;
            pkg.SrcID = userId;   // [typhon] 所有出包带 src_id = user_id (网关据此校验 token.user_id)
            int total = encode(pkg, pkSendBuf);

            if (safeKcp!.SendFlush(pkSendBuf, 0, total) != 0)
            {
                throw new Exception("Kcp.Send failed");
            }
        }

        private int encode(Package pkg, byte[] outBuf)
        {// 装包
            Package.Encode16BE(outBuf, Package.OFFSET_ID,     pkg.ID);
            Package.Encode32BE(outBuf, Package.OFFSET_SRC_ID, pkg.SrcID);
            Package.Encode32BE(outBuf, Package.OFFSET_DST_ID, pkg.DstID);
            Package.Encode32BE(outBuf, Package.OFFSET_SEQ,    pkg.Idempotent);

            int plen = pkg.PayloadLength;
            if (safeKcp!.State == Arq.KcpState.Open && plen > 0)
            {
                var nonce = Crypto.MakeNonce(conv, pkg.Idempotent, Crypto.DIR_C2S);
                int clen = Crypto.Encrypt(txKey!, nonce, pkg.Payload, 0, plen, outBuf, Package.HEADER_SIZE);
                return Package.HEADER_SIZE + clen;
            }

            if (plen > 0)
            {
                Buffer.BlockCopy(pkg.Payload, 0, outBuf, Package.HEADER_SIZE, plen);
            }

            return Package.HEADER_SIZE + plen;
        }

        private bool decode(byte[] data, int n, Package pkg)
        {// 解包
            if (n < Package.HEADER_SIZE)
            {
                return false;
            }

            Package.Decode16BE(data, Package.OFFSET_ID, out pkg.ID);
            Package.Decode32BE(data, Package.OFFSET_SRC_ID, out pkg.SrcID);
            Package.Decode32BE(data, Package.OFFSET_DST_ID, out pkg.DstID);
            Package.Decode32BE(data, Package.OFFSET_SEQ, out pkg.Idempotent);
            if (pkg.Idempotent == 0)
            {
                return false;
            }

            int plen = n - Package.HEADER_SIZE;
            if (safeKcp!.State == Arq.KcpState.Open && plen > 0)
            {
                var nonce = Crypto.MakeNonce(conv, pkg.Idempotent, Crypto.DIR_S2C);
                int m = Crypto.Decrypt(rxKey!, nonce, data, Package.HEADER_SIZE, plen, pkg.Payload, 0);
                if (m < 0)
                {
                    return false;
                }
                pkg.PayloadLength = m;
            }
            else
            {
                pkg.PayloadLength = plen;
                if (plen > 0)
                {
                    Buffer.BlockCopy(data, Package.HEADER_SIZE, pkg.Payload, 0, plen);
                }
            }

            return true;
        }

        private void registReq()
        {// 鉴权请求
            var pkg = Package.Pool.Take();
            pkg.ID = Package.PKID_REGIST_REQ;
            pkg.DstID = GatewayID;
            Buffer.BlockCopy(token, 0, pkg.Payload, 0, token!.Length);
            pkg.PayloadLength = token.Length;
            doSend(pkg);
            Package.Pool.Return(pkg);
        }

        private void output(byte[] segment, int size)
        {// kcp set output —— segment 已是 [8B MAC][datagram](信封在 Kcp.Output 内拼好), 直接发
            sock!.SendTo(segment, size, SocketFlags.None, remotePoint!);
        }

        private readonly BlockingQueue<Package> sndQue = new BlockingQueue<Package>();
        private readonly BlockingQueue<IOEvent> rcvQue = new BlockingQueue<IOEvent>();

        // ---- 控制 / 共享 ----
        private int running = 0;
        private Thread? rcvThread = null;
        private Thread? sndThread = null;
        private ISessionEvent? ev = null;
        private EndPoint? remotePoint = null;
        private Socket? sock = null;
        private SafeKcp? safeKcp = null;

        // 事件驱动: IO 线程入队/关闭时通知宿主跑一次 Update
        public Action? OnWakeup;
        private int notifyPending = 0;

        // ---- 身份属性 ----
        private byte[] token = new byte[170];
        private byte[] pk;
        private byte[] sk;

        // ---- 仅 ioRecv 线程 ----
        private byte[] rxKey = new byte[32];
        private uint rcvSeq = 0;
        private byte[] rbuf = new byte[Package.PACK_MAX_LEN + 1];

        // ---- 仅 ioSend 线程 ----
        private byte[] txKey = new byte[32];
        private uint sndSeq = 0;
        private byte[] pkSendBuf = new byte[Package.PACK_MAX_LEN];

        private uint conv = 0;
        private uint userId = 0;
        private string host = "";
        private byte[]? macKey;
        private uint connectStartMs = 0;   // Connect() 时刻, 握手超时用

    }
}
