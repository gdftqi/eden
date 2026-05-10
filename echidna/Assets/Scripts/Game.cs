using Echidna;
using System.Diagnostics;
using System.Net;
using UnityEngine;
using Debug = UnityEngine.Debug;

public class Game : MonoBehaviour, ISessionEvent
{
    [SerializeField] private string host = "127.0.0.1:5555";
    [SerializeField] private uint conv = 1;
    [SerializeField] private float sendIntervalSec = 0.1f;   // 多久发一次 4KB

    private const int DataSize = 4096;

    private System.Random rng = new System.Random();
    private Stopwatch sw;
    private float timeSinceLastSend;
    private byte[] lastSent;        // 最近一次发的内容，用来对比 echo
    private int echoRound;

    void Start()
    {
        sw = Stopwatch.StartNew();
        KcpSession.Instance.SetEvent(this);
        KcpSession.Instance.Connect(conv, host);
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
        var data = new byte[DataSize];
        rng.NextBytes(data);
        lastSent = data;
        echoRound++;
        KcpSession.Instance.Send(data);
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

    public void OnData(byte[] data, int len)
    {
        bool match = (lastSent != null) && (len == lastSent.Length);
        if (match)
        {
            for (int i = 0; i < len; i++)
            {
                if (data[i] != lastSent[i]) { match = false; break; }
            }
        }

        if (match)
        {
            Debug.Log($"echo recv {len} bytes  ✓ match");
        }
        else
        {
            Debug.LogError($"echo recv {len} bytes  ✗ MISMATCH (expected {lastSent?.Length})");
        }
    }
}
