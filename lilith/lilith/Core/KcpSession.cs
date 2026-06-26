using lilith.Tools;
using System;
using System.Diagnostics;
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

    enum IOEventType : byte
    {// IO 事件 类型
        Connected, // 连接
        RcvData // 接收数据
    }

    struct IOEvent
    {// IO 事件
        public readonly IOEventType Type;
        public readonly Package? Pkg;

        public IOEvent(IOEventType type, Package? pkg = null)
        {
            Type = type;
            Pkg = pkg;
        }
    }

    public class KcpSession
    {// Kcp 会话
        const int UDP_MTU = 1450;   // 必须与服务端 core::typhon.in.hpp 的 UDP_MTU 一致
        const int TICK_INTERVAL_MS = 10;
        const int SEND_BATCH = 128;
        const int RECV_BATCH = 128;

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

        public void Init(string host, uint conv, uint userId, string b64Token, uint gwId)
        {
            this.host = host;
            this.conv = conv;
            this.userId = userId;
            token = Crypto.Base64DecodeToBytes(b64Token);
            authed = false;
            sndSeq = rcvSeq = 0;
            GatewayID = gwId;
        }

        public void Connect(ISessionEvent ev)
        {// 连接服务
            if (ev == null)
            {// 入参检查
                throw new Exception("param is invalid");
            }

            this.ev = ev;

            if (Interlocked.CompareExchange(ref running, 1, 0) != 0)
            {// 是否运行
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
                remotePoint = new IPEndPoint(IPAddress.Parse(ipStr), port);
                sock = new Socket(AddressFamily.InterNetwork, SocketType.Dgram, ProtocolType.Udp);
                sock.Bind(new IPEndPoint(IPAddress.Any, 0));

                var kcp = new Kcp(conv, output);
                kcp.SetSipHash(Crypto.SIPHASH_KEY);   // [typhon] 出站信封 MAC key, 服务端 XDP 据此校验
                kcp.SetNoDelay(1, TICK_INTERVAL_MS, 3, true);
                kcp.SetMtu(UDP_MTU - Crypto.ENVELOPE_MAC_LEN);
                kcp.SetPing(true);          // [typhon] 客户端: 空闲时主动发 PING 保活
                kcp.SetTimeout(30000);      // [typhon] 30s 超时; PING 间隔 = timeout/3 = 10s
                safeKcp = new SafeKcp(kcp);

                rcvThread = new Thread(rcvLoop) { IsBackground = true };
                sndThread = new Thread(sndLoop) { IsBackground = true };
                rcvThread.Start();
                sndThread.Start();
            }
            catch
            {
                running = 0;
                sock?.Close();
                throw;
            }
        }

        public void Close()
        {// 关闭连接
            if (Interlocked.CompareExchange(ref running, 0, 1) == 1)
            {
                sock?.Close();
                sendQue.Signal();
                Notify();
            }
        }

        public void Update()
        {// 刷新 Package 句柄
            Interlocked.Exchange(ref notifyPending, 0);
            if (!Running)
            {
                if (rcvThread != null || sndThread != null)
                {
                    rcvThread?.Join();
                    sndThread?.Join();
                    rcvThread = null;
                    sndThread = null;
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

            var evs = new IOEvent[RECV_BATCH];
            int n = recvQue.Wait(evs);

            for (int i = 0; i < n; i++)
            {
                switch (evs[i].Type)
                {
                    case IOEventType.Connected:
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
            if (Interlocked.Exchange(ref notifyPending, 1) == 0)
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

            var copy = Package.Pool.Take();
            copy.CopyFrom(pkg);
            sendQue.Enqueue(copy);
        }

        private void rcvLoop()
        {// 接收线程
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

                    // [typhon] 信封 MAC 的剥离/长度校验已下沉到 Kcp.Input(和 C++ ikcp_input 一致), 这里直接喂整包
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
                Debug.WriteLine("recvLoop SocketException: {0} ({1}) {2}", ex.SocketErrorCode, ex.ErrorCode, ex.Message);
            }
            catch (Exception ex)
            {
                Debug.WriteLine("recvLoop error: " + ex);
            }
            finally
            {
                Close();
            }
        }

        private void onRegistRsp(Package pkg)
        {// PKID_REGIST_RSP 句柄
            if (authed)
            {// 已鉴权
                return;
            }

            if (pkg.PayloadLength < 32)
            {
                return;
            }

            Crypto.X25519KxClient(sk, pk, pkg.Payload, out rxKey, out txKey);
            authed = true;
            recvQue.Enqueue(new IOEvent(IOEventType.Connected));
            Notify();
            Package.Pool.Return(pkg);
        }

        private void onPong(Package pkg)
        {// PKID_PONG 句柄
            if (!authed)
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
            if (!authed || pkg.Idempotent <= rcvSeq)
            {
                Package.Pool.Return(pkg);
                return;
            }

            rcvSeq = pkg.Idempotent;
            recvQue.Enqueue(new IOEvent(IOEventType.RcvData, pkg));
            Notify();
        }

        private void sndLoop()
        {// 发送线程
            try
            {
                registReq();
                var pks = new Package[SEND_BATCH];
                int wait = 0;
                while (Running)
                {
                    int n = sendQue.Wait(pks, wait);
                    for (int i = 0; i < n; i++)
                    {
                        doSend(pks[i]);
                        Package.Pool.Return(pks[i]);
                    }

                    uint tnow = (uint)Environment.TickCount;
                    safeKcp!.Update(tnow);

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
                Debug.WriteLine("sendLoop SocketException: {0} ({1}) {2}", ex.SocketErrorCode, ex.ErrorCode, ex.Message);
            }
            catch (Exception ex)
            {
                Debug.WriteLine("sendLoop error: " + ex);
            }
            finally
            {
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
            if (authed && plen > 0)
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
            if (authed && plen > 0)
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

        private readonly BlockingQueue<Package> sendQue = new BlockingQueue<Package>();
        private readonly BlockingQueue<IOEvent> recvQue = new BlockingQueue<IOEvent>();

        // ---- 控制 / 共享 ----
        private int running = 0;
        private Thread? rcvThread = null;
        private Thread? sndThread = null;
        private ISessionEvent? ev = null;
        private EndPoint? remotePoint = null;
        private Socket? sock = null;
        private SafeKcp? safeKcp = null;
        private volatile bool authed = false;

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
    }
}
