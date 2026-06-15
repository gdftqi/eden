using kcp2k;
using System;
using System.Collections.Concurrent;
using System.Net;
using System.Net.Sockets;
using System.Threading;


namespace Echidna
{
    /// <summary>会话事件</summary>
    public interface ISessionEvent
    {
        void OnConnected(IPEndPoint host);     // 鉴权握手成功后触发(不是 UDP 一连上)
        void OnDisconnected(IPEndPoint host);
        void OnPackage(Package pkg);
    }


    /// <summary>
    /// typhon KCP 客户端会话(单线程驱动 + 后台 UDP 收线程)。
    /// 完整链路(对齐 examples/kcp_echo/test_kcp.py):
    ///   envelope MAC(SipHash) → KCP → REGIST 握手(写死 token) → ChaCha20-Poly1305 加密收发。
    /// </summary>
    public class KcpSession
    {
        static KcpSession instance;
        public static KcpSession Instance => instance ??= new KcpSession();
        private KcpSession() { }

        public bool Running => Interlocked.CompareExchange(ref running, 0, 0) == 1;

        public void SetEvent(ISessionEvent ev)
        {
            this.ev = ev ?? throw new Exception("ev is invalid");
        }


        /// <summary>
        /// 连接网关。clientId ∈ [0, TOKENS 数量) —— 决定 conv(=2000+id) 与写死的 token。
        /// </summary>
        public void Connect(int clientId, string host)
        {
            if (ev == null) throw new Exception("SessionEvent is invalid");
            if (string.IsNullOrEmpty(host)) throw new Exception("host is null or empty");

            var ipStr   = host.Substring(0, host.IndexOf(':'));
            var portStr = host.Substring(host.LastIndexOf(":") + 1);
            if (!int.TryParse(portStr, out int port)) throw new Exception("host is invalid");

            if (Interlocked.CompareExchange(ref running, 1, 0) != 0) return;

            try
            {
                this.clientId = clientId;
                this.conv     = Crypto.Conv(clientId);
                this.token    = Crypto.Token(clientId);
                authed   = false;
                rxKey    = txKey = null;
                sndSeq   = rcvSeq = 0;

                remotePoint = new IPEndPoint(IPAddress.Parse(ipStr), port);
                udp = new UdpClient(new IPEndPoint(IPAddress.Any, 0));
                udp.Connect(remotePoint);

                kcp = new Kcp(conv, output);
                kcp.SetNoDelay(1, 10, 3, true);
                kcp.SetMtu(1392);   // = UDP_MTU(1400) - envelope MAC(8), 对齐服务端 KCP_MTU / test_kcp.py

                udpRecvThread = new Thread(udpRecvLoop) { IsBackground = true };
                udpRecvThread.Start();

                // 发 REGIST_REQ(明文, authed 还是 false 所以不加密): payload = 写死 token
                SendRegistReq();
            }
            catch
            {
                running = 0;
                udp?.Close();
                throw;
            }
        }

        public void Stop()
        {
            if (Interlocked.CompareExchange(ref running, 0, 1) == 1)
                udp.Close();
        }


        private void udpRecvLoop()
        {
            IPEndPoint remote = null;
            while (Running)
            {
                try
                {
                    var data = udp.Receive(ref remote);
                    rbufQueue.Enqueue(data);
                }
                catch (ObjectDisposedException) { break; }   // 正常退出
                catch { /* TODO: log */ }
            }
        }


        public void Update(uint current)
        {
            if (!Running)
            {
                if (udpRecvThread != null)
                {
                    udpRecvThread.Join();
                    udpRecvThread = null;
                    ev?.OnDisconnected(remotePoint);
                    remotePoint = null;
                    rbufQueue.Clear();
                    kcp = null;
                    udp = null;
                    authed = false;
                }
                return;
            }

            // 1. drain UDP → strip 8B envelope MAC → 喂 KCP
            while (rbufQueue.TryDequeue(out byte[] data))
            {
                if (data.Length >= Crypto.ENVELOPE_MAC_LEN)
                    kcp.Input(data, Crypto.ENVELOPE_MAC_LEN, data.Length - Crypto.ENVELOPE_MAC_LEN);
            }

            // 2. 取出完整 KCP 消息 → 握手 / 业务分流
            while (true)
            {
                int n = kcp.Receive(rbuf, rbuf.Length);
                if (n <= 0) break;
                OnKcpMessage(n);
            }

            kcp.Update(current);
        }


        // 一条完整 KCP 消息: rbuf[0..n) = 明文 10B 头 + (密文 payload + 16B tag) 或明文 payload
        private void OnKcpMessage(int n)
        {
            if (n < Package.HEADER_SIZE) return;

            // 解出 seq(nonce 用) —— 头永远明文
            Package.Decode32BE(rbuf, Package.OFFSET_SEQ, out uint seq);

            if (!authed)
            {
                // 期望 REGIST_RSP(明文): payload = 服务端临时 X25519 公钥(32B)
                if (!Package.Unpack(rbuf, n, recvPkg)) return;
                if (recvPkg.PkId == Package.PKID_REGIST_RSP && recvPkg.PayloadLength >= 32)
                {
                    var srvPk = new byte[32];
                    Buffer.BlockCopy(recvPkg.Payload, 0, srvPk, 0, 32);
                    Crypto.KxClient(srvPk, out rxKey, out txKey);   // 派生会话密钥
                    authed = true;
                    ev?.OnConnected(remotePoint);
                }
                return;
            }

            // authed: 业务包。有 payload 时 = 头 + 密文 + tag, 需解密
            int plen = n - Package.HEADER_SIZE;
            if (plen > 0)
            {
                if (plen < Crypto.AEAD_TAG_LEN) return;             // 连 tag 都放不下
                var body = new byte[plen];
                Buffer.BlockCopy(rbuf, Package.HEADER_SIZE, body, 0, plen);
                var nonce = Crypto.MakeNonce(conv, seq, Crypto.DIR_S2C);
                var plain = Crypto.Decrypt(rxKey, nonce, body, plen);
                if (plain == null) return;                          // 验签失败, 丢弃
                Buffer.BlockCopy(plain, 0, rbuf, Package.HEADER_SIZE, plain.Length);
                n = Package.HEADER_SIZE + plain.Length;             // 剥掉 tag 后的明文长度
            }

            if (!Package.Unpack(rbuf, n, recvPkg)) return;
            if (recvPkg.PkSeq <= rcvSeq) return;                    // 幂等去重
            rcvSeq = recvPkg.PkSeq;
            ev?.OnPackage(recvPkg);
        }


        /// <summary>
        /// 发送一个业务 Package。自动 stamp 单调递增 PkSeq; authed 后自动加密 payload + 附 tag。
        /// pkg 可池化复用,返回后即可归还。
        /// </summary>
        public void SendPk(Package pkg)
        {
            if (!Running || kcp == null) throw new Exception("Kcp session is not running");

            pkg.PkSeq = ++sndSeq;
            int total = Package.Pack(pkg, pkSendBuf);   // 明文 头 + payload, total = 10 + plen

            // authed 且有 payload → 原地把 payload 换成 密文 + 16B tag
            if (authed && pkg.PayloadLength > 0)
            {
                var nonce = Crypto.MakeNonce(conv, pkg.PkSeq, Crypto.DIR_C2S);
                var enc = Crypto.Encrypt(txKey, nonce, pkg.Payload, pkg.PayloadLength); // = plen + 16
                Buffer.BlockCopy(enc, 0, pkSendBuf, Package.HEADER_SIZE, enc.Length);
                total = Package.HEADER_SIZE + enc.Length;
            }

            if (kcp.Send(pkSendBuf, 0, total) != 0)
                throw new Exception("Kcp.Send failed");
        }


        // 握手第一包: REGIST_REQ, dst_id = GATEWAY_ID, payload = 写死 token(明文)
        private void SendRegistReq()
        {
            var pkg = Package.Pool.Take();
            pkg.Reset();
            pkg.PkId    = Package.PKID_REGIST_REQ;
            pkg.PkDstId = Package.GATEWAY_ID;
            Buffer.BlockCopy(token, 0, pkg.Payload, 0, token.Length);
            pkg.PayloadLength = token.Length;
            SendPk(pkg);                 // authed=false → 不加密
            Package.Pool.Return(pkg);
        }


        // KCP output: prepend 8B envelope MAC(SipHash 覆盖 KCP frame 前 24B), 再 udp.Send
        private void output(byte[] segment, int size)
        {
            var mac = Crypto.SipHashTag(segment, Math.Min(size, Crypto.ENVELOPE_MAC_HASH_LEN));
            Buffer.BlockCopy(mac, 0, udpSendBuf, 0, Crypto.ENVELOPE_MAC_LEN);
            Buffer.BlockCopy(segment, 0, udpSendBuf, Crypto.ENVELOPE_MAC_LEN, size);
            udp.Send(udpSendBuf, Crypto.ENVELOPE_MAC_LEN + size);
        }


        // ---- 状态 ----
        private IPEndPoint remotePoint;
        private UdpClient  udp;
        private Kcp        kcp;
        private int        running = 0;

        private int    clientId;
        private uint   conv;
        private byte[] token;
        private bool   authed;
        private byte[] rxKey, txKey;   // 会话密钥(32B), 握手后派生
        private uint   sndSeq, rcvSeq; // 上行单调递增 / 下行幂等去重

        private ConcurrentQueue<byte[]> rbufQueue = new ConcurrentQueue<byte[]>();
        private Thread        udpRecvThread;
        private ISessionEvent ev;

        // 接收 / 发送缓冲(单线程, 复用)
        private byte[]  rbuf        = new byte[Package.PACK_MAX_LEN + 1];
        private byte[]  pkSendBuf   = new byte[Package.PACK_MAX_LEN];
        private byte[]  udpSendBuf  = new byte[Package.PACK_MAX_LEN + Crypto.ENVELOPE_MAC_LEN];
        private Package recvPkg     = new Package();
    }
}
