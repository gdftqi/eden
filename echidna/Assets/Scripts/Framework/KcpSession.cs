using kcp2k;
using System;
using System.Collections.Concurrent;
using System.Net;
using System.Net.Sockets;
using System.Threading;
using UnityEngine;


namespace Echidna
{
    public interface ISessionEvent
    {
        public void OnConnected(IPEndPoint host);
        public void OnDisconnected(IPEndPoint host);
        public void OnData(byte[] data, int len);
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
            if (ev == null) throw new Exception("ev is invalid");
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
                    kcp = new Kcp(conv, output);
                    kcp.SetNoDelay(1, 10, 3, true);

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
                    if (remotePoint.Equals(remote))
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
                if (n <= 0) break;

                ev?.OnData(rbuf.Array, n);
            }

            kcp.Update(current);
        }

        public void Send(byte[] data)
        {
            if (Running && kcp.Send(data, 0, data.Length) != 0)
                throw new Exception("Kcp.Send failed");
        }

        private void output(byte[] segment, int size)
        {
            udp.Send(segment, size, remotePoint);
        }

        private IPEndPoint remotePoint;
        private UdpClient udp;
        private Kcp kcp;
        private int running = 0;
        private ConcurrentQueue<byte[]> rbufQueue = new ConcurrentQueue<byte[]>();
        Thread udpRecvThread;
        ISessionEvent ev;
        ArraySegment<byte> rbuf = new ArraySegment<byte>(new byte[1024 * 8]);
    }
}