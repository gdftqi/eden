using Echidna;
using System.Diagnostics;
using System.Net;
using UnityEngine;
using Debug = UnityEngine.Debug;

public class Game : MonoBehaviour, ISessionEvent
{
    [SerializeField] private string host = "127.0.0.1:5555";
    [SerializeField] private int clientId = 0;   // token/conv 索引: conv = 2000 + clientId (范围 0..11)
    [SerializeField] private float sendIntervalSec = 0.1f; // 发送间隔

    private const int DataSize = 4096;

    private System.Random rng = new System.Random();
    private Stopwatch sw;
    private float timeSinceLastSend;
    private readonly byte[] lastSent = new byte[DataSize];  // 复用,避免每帧分配
    private int echoRound;

    void Start()
    {
        sw = Stopwatch.StartNew();
        KcpSession.Instance.SetEvent(this);
        KcpSession.Instance.Connect(clientId, host);
    }

    void Update()
    {
        // 推 KCP 状态机（必须每帧调，否则重传 / ACK / flush 都会停）
        uint nowMs = (uint)sw.ElapsedMilliseconds;
        KcpSession.Instance.Update(nowMs);

        // 周期性发 4KB random
        timeSinceLastSend += Time.deltaTime;
        if (timeSinceLastSend >= sendIntervalSec)
        {
            timeSinceLastSend = 0f;
            SendRandom4K();
        }
    }

    void OnApplicationQuit()
    {
        KcpSession.Instance.Stop();
    }

    private void SendRandom4K()
    {
        // 1. 先把随机字节填到 lastSent (留一份本地副本,后面比对 echo 用)
        rng.NextBytes(lastSent);
        echoRound++;

        // 2. 从池取一个 Package,填字段 + 拷 payload
        var pkg = Package.Pool.Take();
        pkg.PkId          = Package.PK_ID_PING;
        pkg.PkDstId       = Package.PK_DST_ID;   // 业务包路由到后端 service (dst_id 必须 > 0)
        pkg.PayloadLength = DataSize;
        System.Buffer.BlockCopy(lastSent, 0, pkg.Payload, 0, DataSize);

        // 3. 发出 + 立即归还池 (KcpSession 内部已把字节拷到 pkSendBuf)
        KcpSession.Instance.SendPk(pkg);
        Package.Pool.Return(pkg);

        Debug.Log($"[round {echoRound}] sent {DataSize} bytes");
    }

    // ---- ISessionEvent ----

    public void OnConnected(IPEndPoint host)
    {
        Debug.Log($"connected to {host}");
    }

    public void OnDisconnected(IPEndPoint host)
    {
        Debug.Log($"disconnected from {host}");
    }

    public void OnPackage(Package pkg)
    {
        // pkg 在本函数返回后会被 KcpSession 复用;要存就立刻拷走
        bool match = (pkg.PayloadLength == DataSize);
        if (match)
        {
            for (int i = 0; i < DataSize; i++)
            {
                if (pkg.Payload[i] != lastSent[i]) { match = false; break; }
            }
        }

        if (match)
        {
            Debug.Log($"echo recv {pkg.PayloadLength} bytes  ✓ match  (pk_seq={pkg.PkSeq})");
        }
        else
        {
            Debug.LogError($"echo recv {pkg.PayloadLength} bytes  ✗ MISMATCH (expected {DataSize})");
        }
    }
}
