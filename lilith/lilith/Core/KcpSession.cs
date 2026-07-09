using Lilith.Core.Arq;
using Lilith.Utils;
using System;
using System.Collections.Generic;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using System.Threading.Tasks;


namespace Lilith.Core
{
    /// <summary>
    /// KCP 会话
    /// </summary>
    public class KcpSession
    {
        /// <summary>
        /// UDP MTU
        /// </summary>
        public const int UDP_MTU = 1450;

        /// <summary>
        /// 握手超时 5秒
        /// </summary>
        public const uint HANDSHAKE_TIMEOUT_MS = 5000;

        
        static KcpSession instance = new KcpSession();

        /// <summary>
        /// 单例
        /// </summary>
        public static KcpSession Instance
        {
            get { return instance; }
        }


        /// <summary>
        /// 构造函数
        /// </summary>
        private KcpSession()
        {
            Crypto.X25519KeyGen(out pk, out sk);
        }


        #region /// 属性

        /// <summary>
        /// KCP 状态
        /// </summary>
        public KcpState State
        {
            get
            {
                return sKcp?.State ?? KcpState.None;
            }
        }


        /// <summary>
        /// 网关ID
        /// </summary>
        public uint GatewayID
        {
            get
            {
                return gatewayId;
            }
        }


        /// <summary>
        /// IO 是否处理活跃状态
        /// </summary>
        public bool Activated
        {
            get
            {
                return Interlocked.CompareExchange(ref activated, 0, 0) == 1;
            }
        }


        /// <summary>
        /// 公钥
        /// </summary>
        public byte[] PK
        {
            get { return pk; }
        }


        /// <summary>
        /// 私钥
        /// </summary>
        public byte[] SK
        {
            get { return sk; }
        }

        #endregion /// 属性


        #region /// 流式初始化

        /// <summary>
        /// 设置 kcp server 地址
        /// </summary>
        /// <param name="host"></param>
        /// <returns></returns>
        public KcpSession SetHost(string host)
        {
            this.host = host;
            return this;
        }


        /// <summary>
        /// 设置 kcp conv
        /// </summary>
        /// <param name="conv"></param>
        /// <returns></returns>
        public KcpSession SetConv(uint conv)
        {
            this.conv = conv;
            return this;
        }


        /// <summary>
        /// 设置 UserID
        /// </summary>
        /// <param name="userId"></param>
        /// <returns></returns>
        public KcpSession SetUserID(uint userId)
        {
            this.userId = userId;
            return this;
        }


        /// <summary>
        /// 设置网关ID
        /// </summary>
        /// <param name="gatewayId"></param>
        /// <returns></returns>
        public KcpSession SetGatewayID(uint gatewayId)
        {
            this.gatewayId = gatewayId;
            return this;
        }


        /// <summary>
        /// 设置 Envelope MAC KEY
        /// </summary>
        /// <param name="macKey"></param>
        /// <returns></returns>
        public KcpSession SetMacKey(string macKey)
        {
            this.macKey = Encoding.ASCII.GetBytes(macKey);
            return this;
        }


        /// <summary>
        /// 设置访问TOKEN, 用于网关鉴权
        /// </summary>
        /// <param name="b64Token"></param>
        /// <returns></returns>
        public KcpSession SetAccessToken(string b64Token)
        {
            accessToken = Crypto.Base64DecodeToBytes(b64Token);
            return this;
        }


        /// <summary>
        /// 设置 KCP 存活时间
        /// </summary>
        /// <param name="timeout"></param>
        /// <returns></returns>
        public KcpSession SetTimeout(uint timeout)
        {
            this.timeoutMs = timeout * 1000;
            return this;
        }

        #endregion /// 流式初始化


        /// <summary>
        /// 打开会话, 成功返回时 State 已为 Open, 可直接收发业务.
        /// </summary>
        /// <returns>
        ///     1: 已有并发 Open 在进行, 本次未执行(结果以那次为准)<br/>
        ///     0: 成功 —— 握手完成, State=Open<br/>
        ///    -1: 失败 —— host 非法(端口解析失败)或过程中抛异常(IP 解析/绑定/IO 等), 已 Close<br/>
        /// -1000: 失败 —— REGIST_REQ 发送失败, 已 Close<br/>
        /// -1001: 失败 —— 等待 REGIST_RSP 失败(握手超时或期间被判死), 已 Close
        /// </returns>
        public async Task<int> Open()
        {
            var ipStr = host.Substring(0, host.IndexOf(':'));
            var portStr = host.Substring(host.LastIndexOf(":") + 1);
            if (!int.TryParse(portStr, out int port))
                return -1;

            if (Interlocked.CompareExchange(ref activated, 1, 0) != 0)
                return 1;

            try
            {
                // 创建 kcp
                var kcp = new Kcp(conv, output);
                kcp.SetSipHash(this.macKey!);
                kcp.SetNoDelay(1, 100, 3, true);
                kcp.SetMtu(UDP_MTU - Crypto.ENVELOPE_MAC_LEN);
                kcp.SetPing(true);
                kcp.SetTimeout(timeoutMs);

                // 创建 safe kcp
                sKcp = new SafeKcp(kcp);

                // 创建 UDP IO
                remotePoint = new IPEndPoint(IPAddress.Parse(ipStr), port);
                sock = new Socket(AddressFamily.InterNetwork, SocketType.Dgram, ProtocolType.Udp);
                sock.Bind(new IPEndPoint(IPAddress.Any, 0));

                // 注册网关
                if (RegistReq() < 0)
                {
                    Close();
                    return -1000;
                }

                // 等待网关注册应答
                if (await WaitRegistRsp().ConfigureAwait(false) < 0)
                {
                    Close();
                    return -1001;
                }

                return 0;
            }
            catch(Exception ex)
            {
                Log.Write($"[KCP] Open() 异常: {ex}");
                Close();

                return -1;
            }
        }


        /// <summary>
        /// 关闭会话
        /// </summary>
        public void Close()
        {
            if (Interlocked.CompareExchange(ref activated, 0, 1) != 1)
                return;

            if (sndSeq != 0)
                sndSeq = 0;

            if (rcvSeq != 0)
                rcvSeq = 0;

            if (sock != null)
            { 
                sock.Close();
                sock = null;
            }

            if (sKcp != null)
                sKcp = null;
        }


        /// <summary>
        /// 推动底层 KCP 状态机(超时重传 / 回 ACK / flush 待发数据), 需按 interval 周期调用。
        /// 返回值兼作判死信号: 返回负值即会话已死, 上层据此摘除会话。
        /// </summary>
        /// <param name="currentMs">当前时间(毫秒)</param>
        /// <returns>
        ///     0: 存活 —— KCP 正常推进, 未判死<br/>
        ///    -1: 超时判死 —— 超过 timeout 未收到对端任何包(KcpState.Timeout)<br/>
        ///    -2: RST 判死 —— 收到对端 RST, 会话已不存在(KcpState.Rst)<br/>
        /// -1000: 会话未打开(未 Running)就调用了 Update
        /// </returns>
        public int Update(uint currentMs)
        {
            if (!Activated)
            {
                Log.Write("会话未打开时调用 Update");
                return -1000;
            }

            var k = sKcp;
            if (k == null)
            {
                Log.Write("会话未打开时调用 Update");
                return -1000;
            }

            return k.Update(currentMs);
        }


        /// <summary>
        /// 发送消息
        /// </summary>
        /// <param name="pkg"></param>
        /// <returns>
        ///     0: 成功 —— 已进 KCP 发送队列并 flush(可靠传输, 但不代表对端已收到)<br/>
        ///    -1: 底层拒绝 —— 长度非法(len&lt;0, 正常流程不会出现)<br/>
        ///    -2: 底层拒绝 —— 包过大, 分片数超过接收窗口(Kcp.Send)<br/>
        /// -1000: 会话未打开(未 Activated)就调用了 Send<br/>
        /// -1001: 未鉴权(State 非 Open)时发送业务包, 被闸门拦下
        /// </returns>
        public int Send(Package pkg)
        {
            if (!Activated)
            {
                Log.Write("会话未打开时调用 Send");
                return -1000;
            }

            var k = sKcp;
            if (k == null)
            {
                Log.Write("会话未打开时调用 Send");
                return -1000;
            }

            if (k.State != KcpState.Open && pkg.ID != Package.PKID_REGIST_REQ)
            {
                Log.Write("会话未鉴权时调用 Send");
                return -1001;
            }

            pkg.Idempotent = ++sndSeq;
            pkg.SrcID = userId;

            int n = Encode(pkg, sbuf);
            return k.SendFlush(sbuf, 0, n);
        }


        /// <summary>
        /// 接收消息
        /// </summary>
        /// <param name="pkList"></param>
        /// <returns>
        ///     0: 正常 —— 含四种情形: 解出 0~N 个包 / 数据报来源非网关(丢弃) / 非阻塞下暂无数据(WouldBlock) /
        ///        Close 抽走 socket(ObjectDisposed, 正常收尾)<br/>
        /// -1000: 会话已关闭 —— 未激活就调用, 或阻塞等待中被 Close 打断(OperationAborted/Interrupted, 正常收尾)<br/>
        /// -错误码: 网络异常 —— 其余 SocketException(如 -10054 对端不可达), 已将 State 置为 Shutdown 判死,
        ///        下一次 Update 将返回对应负值, 上层据此走断线处理
        /// </returns>
        public int Recv(ref LinkedList<Package> pkList)
        {
            if (!Activated)
            {
                Log.Write("会话未激活时调用 Recv");
                return -1000;
            }

            var s = sock;
            if (s == null)
            {
                Log.Write("会话未激活时调用 Recv");
                return -1000;
            }

            var k = sKcp;
            if (k == null)
            {
                Log.Write("会话未激活时调用 Recv");
                return -1000;
            }

            try
            {
                EndPoint remote = new IPEndPoint(IPAddress.Any, 0);
                int n = s.ReceiveFrom(ibuf, ref remote);
                if (!remote.Equals(remotePoint))
                    return 0;

                if (n <= 0)
                    return 0;

                k.Input(ibuf, 0, n);

                do
                {
                    n = k.Receive(rbuf);
                    if (n <= 0)
                        break;

                    var pk = Package.Pool.Take();
                    if (!Decode(rbuf, n, pk))
                    {
                        Package.Pool.Return(pk);
                        continue;
                    }

                    if (pk.Idempotent <= rcvSeq)
                    {
                        Package.Pool.Return(pk);
                        continue;
                    }

                    rcvSeq = pk.Idempotent;
                    pkList.Add(pk);
                } while (true);

                return 0;
            }
            catch (ObjectDisposedException)
            {// Close 抽走 socket, 与 OperationAborted 同路: 正常收尾
                return -1000;
            }
            catch (SocketException ex)
            {
                if (ex.SocketErrorCode == SocketError.WouldBlock)
                    return 0;

                if (!Activated || ex.SocketErrorCode == SocketError.OperationAborted || ex.SocketErrorCode == SocketError.Interrupted)
                    return -1000;

                Log.Write($"[KCP] recvLoop SocketException: {ex.SocketErrorCode} ({ex.ErrorCode}) {ex.Message}");
                var k = sKcp;
                if (k != null)
                    k.State = KcpState.Shutdown;
                return -ex.ErrorCode;
            }
        }


        /// <summary>
        /// 装包
        /// </summary>
        /// <param name="pkg"></param>
        /// <param name="outBuf">KcpSession.sbuf</param>
        /// <returns>返回装包之后的 outBuf 长度</returns>
        private int Encode(Package pkg, byte[] outBuf)
        {
            Package.Encode16BE(outBuf, Package.OFFSET_ID, pkg.ID);
            Package.Encode32BE(outBuf, Package.OFFSET_SRC_ID, pkg.SrcID);
            Package.Encode32BE(outBuf, Package.OFFSET_DST_ID, pkg.DstID);
            Package.Encode32BE(outBuf, Package.OFFSET_IDEM, pkg.Idempotent);

            int plen = pkg.PayloadLength;
            if (sKcp!.State == KcpState.Open && plen > 0)
            {// 加密
                var nonce = Crypto.MakeNonce(conv, pkg.Idempotent, Crypto.DIR_C2S);
                int clen = Crypto.Encrypt(txKey!, nonce, pkg.Payload, 0, plen, outBuf, Package.HEADER_SIZE);
                return Package.HEADER_SIZE + clen;
            }

            if (plen > 0)
                Buffer.BlockCopy(pkg.Payload, 0, outBuf, Package.HEADER_SIZE, plen);

            return Package.HEADER_SIZE + plen;
        }


        /// <summary>
        /// 拆包
        /// </summary>
        /// <param name="data"></param>
        /// <param name="n"></param>
        /// <param name="pkg"></param>
        /// <returns></returns>
        private bool Decode(byte[] data, int n, Package pkg)
        {
            if (n < Package.HEADER_SIZE)
            {
                Log.Write($"接收到来自服务端错误的消息, 长度不满足 Package.HEADER_SIZE({Package.HEADER_SIZE})");
                return false;
            }

            Package.Decode16BE(data, Package.OFFSET_ID, out pkg.ID);
            Package.Decode32BE(data, Package.OFFSET_SRC_ID, out pkg.SrcID);
            Package.Decode32BE(data, Package.OFFSET_DST_ID, out pkg.DstID);
            Package.Decode32BE(data, Package.OFFSET_IDEM, out pkg.Idempotent);
            if (pkg.Idempotent == 0)
            {
                Log.Write($"接收到来自服务端错误的消息, 长度不满足 Package.Idempotent 为 0");
                return false;
            }

            int plen = n - Package.HEADER_SIZE;
            if (sKcp!.State == KcpState.Open && plen > 0)
            {
                var nonce = Crypto.MakeNonce(conv, pkg.Idempotent, Crypto.DIR_S2C);
                // 解密
                int m = Crypto.Decrypt(rxKey!, nonce, data, Package.HEADER_SIZE, plen, pkg.Payload, 0);
                if (m < 0)
                {
                    Log.Write($"接收到来自服务端错误的消息, 消息解密失败");
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


        /// <summary>
        /// 鉴权请求
        /// </summary>
        private int RegistReq()
        {
            var pkg = Package.Pool.Take();
            pkg.ID = Package.PKID_REGIST_REQ;
            pkg.DstID = GatewayID;
            Buffer.BlockCopy(accessToken, 0, pkg.Payload, 0, accessToken!.Length);
            pkg.PayloadLength = accessToken.Length;
            int res = Send(pkg);
            Package.Pool.Return(pkg);
            return res;
        }


        /// <summary>
        /// 等待并处理 REGIST_RSP: 期间周期 Update 驱动 KCP(丢包重传 REGIST_REQ), 收到应答做 kx 并 Open。
        /// 进入时把 socket 切为非阻塞轮询(Recv 无数据立即返回, 每轮 Task.Delay 让出线程),
        /// 退出时恢复阻塞模式供稳态收包循环使用; 超过 HANDSHAKE_TIMEOUT_MS 未 Open 即失败。
        /// </summary>
        /// <returns>0: 握手成功(State=Open); -1: 握手超时; -2: 握手中被 KCP 判死(如收到 RST)</returns>
        private async Task<int> WaitRegistRsp()
        {
            if (sKcp!.State == KcpState.Open)
                return 0;

            const int POLL_MS = 15;
            var start = Environment.TickCount;
            var pkgList = new LinkedList<Package>();

            sock!.Blocking = false;
            try
            {
                while ((uint)(Environment.TickCount - start) < HANDSHAKE_TIMEOUT_MS)
                {
                    if (Update((uint)Environment.TickCount) < 0)
                        return -2;

                    pkgList.Clear();
                    Recv(ref pkgList);

                    foreach (var pkg in pkgList)
                    {
                        if (sKcp!.State != KcpState.Open && pkg.ID == Package.PKID_REGIST_RSP && pkg.PayloadLength == 32)
                        {
                            Crypto.X25519KxClient(sk, pk, pkg.Payload, out rxKey, out txKey);
                            sKcp!.Open();
                        }
                        else
                        {
                            Log.Write($"收到了服务端不应该发送的消息: {pkg.ToString()}");
                        }
                        Package.Pool.Return(pkg);
                    }

                    if (sKcp!.State == KcpState.Open)
                        return 0;

                    await Task.Delay(POLL_MS).ConfigureAwait(false);
                }

                return -1;
            }
            finally
            {// 恢复阻塞模式; 快照防 Close 竞态(sock 可能已被置 null / dispose)
                var s = sock;
                if (s != null)
                {
                    try
                    {
                        s.Blocking = true;
                    }
                    catch (ObjectDisposedException)
                    {
                        // 有意吞掉, Close() 可能在并发线程中调用, 这里不想抛异常
                    }
                }
            }
        }


        private void output(byte[] segment, int size)
        {
            try
            {
                var s = sock;
                if (!Activated || s == null)
                    return;
                    
                s.SendTo(segment, size, SocketFlags.None, remotePoint!);
            }
            catch (ObjectDisposedException)
            {
                // 有意吞掉, Close() 可能在并发线程中调用, 这里不想抛异常
            }
            catch (SocketException)
            {
                var k = sKcp;
                if (k != null)
                    k.State = KcpState.Shutdown;
            }
        }


        // 是否连接, 表示 UDP Socket 是否存活
        private int activated = 0;

        // 对端(服务端) 地址
        private EndPoint? remotePoint;

        // UDP
        private Socket? sock;

        // Safe Kcp
        private SafeKcp? sKcp;

        // kcp server 鉴权 TOKEN
        private byte[] accessToken = new byte[170];

        // X25519公钥, 用于和 网关 交换密钥
        private byte[] pk;

        // X25519私钥, 用于和 网关 交换密钥
        private byte[] sk;

        // 接收序号, 用于接收幂等
        private uint rcvSeq;

        // 发送序号, 用于发送幂等
        private uint sndSeq;

        // 写密钥
        private byte[] txKey = new byte[32];

        // 读密钥
        private byte[] rxKey = new byte[32];

        // 接收缓冲区, 在 kcp decode Package 的时候使用
        private byte[] rbuf = new byte[Package.PACK_MAX_LEN + 1];

        // 发送缓冲区, 在 kcp 发送 Package 的时候使用
        private byte[] sbuf = new byte[Package.PACK_MAX_LEN + 1];

        // IO 缓冲区, 在 udp recvFrom 的时候使用
        private byte[] ibuf = new byte[UDP_MTU];

        // KCP conv
        private uint conv;

        // 用户ID
        private uint userId;

        // Kcp Server
        private string host = "";

        // Kcp Server 网关ID
        private uint gatewayId;

        // envelope mac 密钥
        private byte[]? macKey;

        // KCP 保活值, 超这该值, KCP 将判 DEAD
        private uint timeoutMs;

    }
}