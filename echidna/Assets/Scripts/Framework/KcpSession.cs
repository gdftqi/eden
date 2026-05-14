using kcp2k;
using System;
using System.Collections.Concurrent;
using System.Net;
using System.Net.Sockets;
using System.Threading;
using UnityEngine;


namespace Echidna
{
    /// <summary>
    /// 会话事件
    /// </summary>
    public interface ISessionEvent
    {
        /// <summary>
        /// 连接成功事件
        /// </summary>
        /// <param name="host"></param>
        public void OnConnected(IPEndPoint host);

        /// <summary>
        /// 连接断开事件
        /// </summary>
        /// <param name="host"></param>
        public void OnDisconnected(IPEndPoint host);

        /// <summary>
        /// 消息事件
        /// </summary>
        /// <param name="pkg"></param>
        public void OnPackage(Package pkg);
    }

    public class KcpSession
    {
        static KcpSession instance;
        public static KcpSession Instance
        {
            get
            {
                if (instance == null) instance = new KcpSession();
                return instance;
            }
        }

        private KcpSession() { }


        public bool Running
        {
            get
            {
                return Interlocked.CompareExchange(ref running, 0, 0) == 1;
            }
        }


        public void SetEvent(ISessionEvent ev)
        {
            if (ev == null) 
                throw new Exception("ev is invalid");
            this.ev = ev;
        }


        public void Connect(uint conv, string host)
        {
            if (ev == null)
                throw new Exception("SessionEvent is invalid");

            if (host == null || host.Length == 0)
                throw new Exception("host is null or empty");

            var ipStr = host.Substring(0, host.IndexOf(':'));
            var portStr = host.Substring(host.LastIndexOf(":") + 1);

            int port;
            if (!int.TryParse(portStr, out port))
                throw new Exception("host is invalid");

            if (Interlocked.CompareExchange(ref running, 1, 0) == 0)
            {
                try
                {
                    remotePoint = new IPEndPoint(IPAddress.Parse(ipStr), port);
                    udp = new UdpClient(new IPEndPoint(IPAddress.Any, 0));
                    udp.Connect(remotePoint);

                    kcp = new Kcp(conv, output);
                    kcp.SetNoDelay(1, 10, 3, true);
                    kcp.SetMtu(1232);
                    
                    ev?.OnConnected(remotePoint);
                    udpRecvThread = new Thread(udpRecvLoop);
                    udpRecvThread.Start();
                }
                catch (Exception ex)
                {
                    // TODO: debug ex
                    running = 0;
                    udp?.Close();
                    throw;
                }
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
                catch (ObjectDisposedException)
                {
                    // 正常退出
                    break;
                }
                catch (Exception ex)
                {
                    // todo: log ex
                }
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

                    while (true)
                    {
                        int n = kcp.Receive(rbuf.Array, rbuf.Count);
                        if (n <= 0) break;
                    }

                    rbufQueue.Clear();
                    kcp = null;
                    udp = null;
                }
                return;
            }

            byte[] data;
            while (rbufQueue.TryDequeue(out data))
            {
                if (kcp.Input(data, 0, data.Length) < 0)
                {
                    // TODO
                }
            }

            while (true)
            {
                int n = kcp.Receive(rbuf.Array, rbuf.Count);
                if (n <= 0)
                    break;

                recvPkg.Reset();
                if (!Package.Unpack(rbuf.Array, n, recvPkg)) 
                    continue;

                if (recvPkg.PkIdem <= rcvIdem)
                    continue;

                rcvIdem = recvPkg.PkIdem;
                ev?.OnPackage(recvPkg);
            }

            kcp.Update(current);
        }

        /// <summary>
        /// 发送一个 Package。会自动 stamp 单调递增的 PkIdem 到 pkg 上;
        /// pkg 可以是池化复用的对象,调用方填充 PkId / PkDstId / Payload / PayloadLength 后传入。
        /// 返回后 pkg 可以立即 Return 给池。
        /// </summary>
        public void SendPk(Package pkg)
        {
            if (!Running || kcp == null) 
                throw new Exception("Kcp session is not running");

            pkg.PkIdem = ++sndIdem;
            int total = Package.Pack(pkg, pkSendBuf);
            if (kcp.Send(pkSendBuf, 0, total) != 0)
                throw new Exception("Kcp.Send failed");
        }

        private void output(byte[] segment, int size)
        {
            udp.Send(segment, size);
        }

        /// <summary>
        /// 服务器地址
        /// </summary>
        private IPEndPoint remotePoint;
        private UdpClient udp;
        private Kcp kcp;
        private int running = 0;
        private uint sndIdem = 0;
        private uint rcvIdem = 0;
        private ConcurrentQueue<byte[]> rbufQueue = new ConcurrentQueue<byte[]>();
        Thread udpRecvThread;
        ISessionEvent ev;
        ArraySegment<byte> rbuf = new ArraySegment<byte>(new byte[Package.PACK_MAX_LEN + 1]);
        byte[] pkSendBuf = new byte[Package.PACK_MAX_LEN];
        Package recvPkg = new Package();   // 接收端复用的 Package 实例(单线程,无需锁)
    }
}