using kcp2k;
using lilith.Utils;
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
        Connected, 
        Data
    }

    struct IOEvent
    {// IO 事件
        public readonly IOEventType Type;
        public readonly Package? Pkg;

        public IOEvent(IOEventType type, Package? pkg)
        {
            Type = type;
            Pkg = pkg;
        }
    }

    public class KcpSession
    {// Kcp 会话
        const int UDP_MTU = 1400;
        const int TICK_INTERVAL_MS = 10;

        static KcpSession instance = new KcpSession();

        public static KcpSession Instance
        {// 单例
            get { return instance; }
        }

        private KcpSession() { }

        public bool Running
        {// 是否运行中
            get
            {
                return Interlocked.CompareExchange(ref running, 0, 0) == 1;
            }
        }

        public void SetEvent(ISessionEvent ev)
        {// 设置事件
            if (ev == null)
            {
                throw new Exception("ISessionEvent is invalid");
            }
            this.ev = ev;
        }

        public void Connect(uint conv, string host)
        {// 连接服务
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
                token = Crypto.Token(conv);
                authed = false;
                sndSeq = rcvSeq = 0;

                remotePoint = new IPEndPoint(IPAddress.Parse(ipStr), port);
                sock = new Socket(AddressFamily.InterNetwork, SocketType.Dgram, ProtocolType.Udp);
                sock.Bind(new IPEndPoint(IPAddress.Any, 0));   // 收前必须 Bind 本地端口(ReceiveFrom 要求), 端口取临时

                var kcp = new Kcp(conv, output);
                kcp.SetNoDelay(1, TICK_INTERVAL_MS, 3, true);
                kcp.SetMtu(UDP_MTU - Crypto.ENVELOPE_MAC_LEN);
                safeKcp = new SafeKcp(kcp);

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
        {// 关闭连接
            if (Interlocked.CompareExchange(ref running, 0, 1) == 1)
            {
                sock?.Close();
                sendQue.Signal();
            }
        }

        public void Update()
        {// 主线程调用
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

            while (recvQue.TryDequeue(out IOEvent e))
            {
                switch (e.Type)
                {
                    case IOEventType.Connected:
                        ev?.OnConnected(remotePoint!);
                        break;

                    case IOEventType.Data:
                        ev?.OnPackage(e.Pkg!);
                        Package.Pool.Return(e.Pkg!);
                        break;
                }
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

        private void recvLoop()
        {// 接收线程
            EndPoint remote = new IPEndPoint(IPAddress.Any, 0);
            var recvBuf = new byte[UDP_MTU];
            const int MIN_SIZE = Crypto.ENVELOPE_MAC_LEN + Crypto.ENVELOPE_MAC_HASH_LEN;
            int n = 0;

            try
            {
                while (Running)
                {
                    n = sock!.ReceiveFrom(recvBuf, UDP_MTU, SocketFlags.None, ref remote);
                    if (n < MIN_SIZE)
                    {
                        continue;
                    }

                    if (!remote.Equals(remotePoint))
                    {// 只收来自服务器的包, 其它来源丢弃
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

                        switch (pkg.PkId)
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

        private bool decode(byte[] data, int n, Package pkg)
        {// 解包
            if (n < Package.HEADER_SIZE)
            {
                return false;
            }

            Package.Decode16BE(data, Package.OFFSET_ID, out pkg.PkId);
            Package.Decode32BE(data, Package.OFFSET_SEQ, out pkg.PkSeq);
            Package.Decode32BE(data, Package.OFFSET_DST_ID, out pkg.PkDstId);
            if (pkg.PkSeq == 0)
            {
                return false;
            }

            int plen = n - Package.HEADER_SIZE;
            if (authed && plen > 0)
            {
                var nonce = Crypto.MakeNonce(conv, pkg.PkSeq, Crypto.DIR_S2C);
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

            Crypto.KxClient(pkg.Payload, out rxKey, out txKey);
            authed = true;
            recvQue.Enqueue(new IOEvent(IOEventType.Connected, null));
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
            if (!authed || pkg.PkSeq <= rcvSeq)
            {
                Package.Pool.Return(pkg);
                return;
            }

            rcvSeq = pkg.PkSeq;
            recvQue.Enqueue(new IOEvent(IOEventType.Data, pkg));
        }

        private void sendLoop()
        {// 发送线程
            try
            {
                registReq();
                while (Running)
                {
                    while (sendQue.TryDequeue(out Package pkg))
                    {
                        doSend(pkg);
                        Package.Pool.Return(pkg);
                    }

                    uint now = (uint)Environment.TickCount;
                    safeKcp!.Update(now);

                    int wait = (int)(safeKcp!.Check(now) - now);
                    if (wait < 0)
                    {
                        wait = 0;
                    }

                    if (wait > TICK_INTERVAL_MS)
                    {
                        wait = TICK_INTERVAL_MS;
                    }
                    sendQue.Wait(wait);
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
            pkg.PkSeq = ++sndSeq;
            int total = encode(pkg, pkSendBuf);

            if (safeKcp!.SendFlush(pkSendBuf, 0, total) != 0)
            {
                throw new Exception("Kcp.Send failed");
            }
        }

        private int encode(Package pkg, byte[] outBuf)
        {// 装包
            Package.Encode16BE(outBuf, Package.OFFSET_ID,     pkg.PkId);
            Package.Encode32BE(outBuf, Package.OFFSET_SEQ,    pkg.PkSeq);
            Package.Encode32BE(outBuf, Package.OFFSET_DST_ID, pkg.PkDstId);

            int plen = pkg.PayloadLength;
            if (authed && plen > 0)
            {
                var nonce = Crypto.MakeNonce(conv, pkg.PkSeq, Crypto.DIR_C2S);
                int clen = Crypto.Encrypt(txKey!, nonce, pkg.Payload, 0, plen, outBuf, Package.HEADER_SIZE);
                return Package.HEADER_SIZE + clen;
            }

            if (plen > 0)
            {
                Buffer.BlockCopy(pkg.Payload, 0, outBuf, Package.HEADER_SIZE, plen);
            }

            return Package.HEADER_SIZE + plen;
        }

        private void registReq()
        {// 鉴权请求
            var pkg = Package.Pool.Take();
            pkg.PkId = Package.PKID_REGIST_REQ;
            pkg.PkDstId = Package.GATEWAY_ID;
            Buffer.BlockCopy(token, 0, pkg.Payload, 0, token!.Length);
            pkg.PayloadLength = token.Length;
            doSend(pkg);
            Package.Pool.Return(pkg);
        }

        private void output(byte[] segment, int size)
        {// kcp set output
            var mac = Crypto.SipHashTag(segment, Math.Min(size, Crypto.ENVELOPE_MAC_HASH_LEN));
            Buffer.BlockCopy(mac, 0, udpSendBuf, 0, Crypto.ENVELOPE_MAC_LEN);
            Buffer.BlockCopy(segment, 0, udpSendBuf, Crypto.ENVELOPE_MAC_LEN, size);
            size += Crypto.ENVELOPE_MAC_LEN;
            sock!.SendTo(udpSendBuf, size, SocketFlags.None, remotePoint!);
        }

        private static bool MacMatch(byte[] tag, byte[] data)
        {// 匹配 SIP HASH
            for (int i = 0; i < Crypto.ENVELOPE_MAC_LEN; i++)
                if (tag[i] != data[i]) return false;
            return true;
        }

        private readonly BlockingQueue<Package> sendQue = new BlockingQueue<Package>();
        private readonly BlockingQueue<IOEvent> recvQue = new BlockingQueue<IOEvent>();

        // ---- 控制 / 共享 ----
        private int running = 0;
        private Thread? recvThread = null;
        private Thread? sendThread = null;
        private ISessionEvent? ev = null;
        private EndPoint? remotePoint = null;
        private Socket? sock = null;
        private SafeKcp? safeKcp = null;
        private volatile bool authed = false;

        // ---- 身份属性 ----
        private uint conv = 0;
        private byte[] token = new byte[170];

        // ---- 仅 ioRecv 线程 ----
        private byte[] rxKey = new byte[32];
        private uint rcvSeq = 0;
        private byte[] rbuf = new byte[Package.PACK_MAX_LEN + 1];

        // ---- 仅 ioSend 线程 ----
        private byte[] txKey = new byte[32];
        private uint sndSeq = 0;
        private byte[] pkSendBuf = new byte[Package.PACK_MAX_LEN];
        private byte[] udpSendBuf = new byte[Package.PACK_MAX_LEN + Crypto.ENVELOPE_MAC_LEN];
    }
}
