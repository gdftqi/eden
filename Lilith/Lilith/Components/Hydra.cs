using Lilith.Core;
using Lilith.Core.Eva;
using Lilith.Utils;
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using System.Threading.Tasks;

namespace Lilith.Components
{
    #region /// enum HydraState 状态枚举

    /// <summary>
    /// Hydra 状态枚举
    /// </summary>
    public enum HydraState
    {
        /// <summary>
        /// 未连接 -> 需重新登录
        /// </summary>
        Disconnected,

        /// <summary>
        /// 连接中 -> RA 登录 + KCP 握手中
        /// </summary>
        Connecting,

        /// <summary>
        /// 已连接 -> 可收发业务
        /// </summary>
        Connected,

        /// <summary>
        /// 重连中 -> Hydra 正在 RA 刷新 + 重连
        /// </summary>
        Reconnecting,
    }

    #endregion /// enum HydraState 状态枚举


    /// <summary>
    /// Hydra: HTTP(RA)
    /// </summary>
    public class Hydra
    {
        // SndLoop 每轮最多取的待发包数
        const int SND_BATCH = 64;

        // Update 每轮从 rcvQue 取包的批大小
        const int RCV_BATCH = 64;

        // SndLoop 的 tick 间隔(毫秒): sndQue.Wait 的超时兼作 KCP Update 周期
        const int TICK_MS = 10;

        // RcvLoop 在会话不可收(已关/重连中)时的退避间隔(毫秒)
        const int IDLE_MS = 15;

        // HTTP 会话保活间隔(毫秒).
        const int PING_MS = 5 * 60 * 1000;


        static readonly Hydra instance = new Hydra();

        /// <summary>
        /// 单例模式
        /// </summary>
        public static Hydra Instance
        {
            get
            {
                return instance;
            }
        }


        /// <summary>
        /// 构造函数
        /// </summary>
        private Hydra()
        {
            State = HydraState.Disconnected;
        }


        /// <summary>
        /// 状态
        /// </summary>
        public HydraState State { get; private set; }

        /// <summary>
        /// [typhon] 被服务端踢除的原因码; 0 表示"本次断开不是被踢".
        /// 上层在 OnStateChanged 收到 Disconnected 时查它, 非 0 就用
        /// <see cref="Package.ErrorText"/> 给用户提示具体原因.
        /// 每次发起连接(Login/重连)时清零, 不会串上一次的值.
        /// </summary>
        public uint KickCode { get; private set; }


        /// <summary>
        /// 关闭代理: 停掉收发线程并关闭 KCP 连接.再次 Login 会重新拉起线程.
        /// </summary>
        public void Close()
        {
            if (Interlocked.CompareExchange(ref running, 0, 1) != 1)
            {
                return;
            }

            StopPing();
            sndQue.Signal();             // 唤醒 SndLoop, 让它看到 running=0 退出
            KcpSession.Instance.Close(); // 抽走 socket, 打断 RcvLoop 的阻塞收包

            DrainToPool(sndQue);
            DrainToPool(rcvQue);

            SetState(HydraState.Disconnected);
        }


        #region /// 流式初始化

        /// <summary>
        ///  设置 HTTP 的 base URL
        /// </summary>
        /// <param name="url"></param>
        /// <returns></returns>
        public Hydra SetHttpBaseUrl(string url)
        {
            HttpSession.Instance.SetBaseUrl(url);
            return this;
        }


        /// <summary>
        /// 设置 HTTP 服务的 X25519 公钥,
        /// 需要这个公钥生成与 HTTP 交互的 tx key 和 rx key.
        /// </summary>
        /// <param name="b64X25519PK"></param>
        /// <returns></returns>
        public Hydra SetHttpX25519PK(string b64X25519PK)
        {
            HttpSession.Instance.SetHttpX25519PK(b64X25519PK);
            return this;
        }


        /// <summary>
        /// 设置 KCP 超时时间(单位秒), 默认 30 秒
        /// </summary>
        /// <param name="timeout"></param>
        /// <returns></returns>
        public Hydra SetKcpTimeout(uint timeout)
        {
            KcpSession.Instance.SetTimeout(timeout);
            return this;
        }


        /// <summary>
        /// 设置鉴权成功后要登记进入的业务后端服务 id 列表
        /// </summary>
        /// <param name="ids"></param>
        /// <returns></returns>
        public Hydra SetEnterServs(uint[] ids)
        {
            KcpSession.Instance.SetEnterServs(ids);
            return this;
        }


        /// <summary>
        /// 设置最大重连时间(单位秒), 默认 5 分钟
        /// </summary>
        /// <param name="max"></param>
        /// <returns></returns>
        public Hydra SetReconnectMax(uint max)
        {
            reconnectMaxMs = max * 1000;
            return this;
        }


        /// <summary>
        /// 重连间隔(单位秒), 默认 5 秒
        /// </summary>
        /// <param name="s"></param>
        /// <returns></returns>
        public Hydra SetReconnectRetryInterval(uint s)
        {
            reconnectRetryIntervalMs = s * 1000;
            return this;
        }


        /// <summary>
        /// 设置 状态改变 事件(prev, cur), 经 Update() 在上层线程派发
        /// </summary>
        /// <param name="handler"></param>
        /// <returns></returns>
        public Hydra SetOnStateChanged(Action<HydraState, HydraState> handler)
        {
            OnStateChanged = handler;
            return this;
        }


        /// <summary>
        /// 设置 收包 事件, 经 Update() 在上层线程派发; 回调返回后包即归还对象池, 不可留引用
        /// </summary>
        /// <param name="handler"></param>
        /// <returns></returns>
        public Hydra SetOnPackage(Action<Package> handler)
        {
            OnPackage = handler;
            return this;
        }


        /// <summary>
        /// 设置 唤醒 事件 </summary>
        /// <param name="handler"></param>
        /// <returns></returns>
        public Hydra SetOnWakeup(Action handler)
        {
            OnWakeup = handler;
            return this;
        }

        #endregion /// 流式初始化


        /// <summary>
        /// 用户登录: RA 登录 + KCP 打开(握手)
        /// </summary>
        /// <param name="username">用户名</param>
        /// <param name="password">密码</param>
        /// <returns>成功返回 true, 否则返回 false </returns>
        public async Task<string> Login(string username, string password)
        {
            KickCode = 0;   // 新一轮连接: 清掉上次的踢人原因
            SetState(HydraState.Connecting);
            try
            {
                if (await UserLogin.POST(username, password) != 0)
                {
                    SetState(HydraState.Disconnected);
                    return "请检查网络";
                }

                if (await KcpSession.Instance.Open() != 0)
                {
                    SetState(HydraState.Disconnected);
                    return "请检查网络";
                }

                Start();
                StartPing();
                SetState(HydraState.Connected);
                return "";
            }
            catch (Exception ex)
            {
                SetState(HydraState.Disconnected);
                return ex.Message;
            }
        }


        /// <summary>
        /// 发送消息.
        /// </summary>
        /// <param name="pkg"></param>
        public void Send(Package pkg)
        {
            sndQue.Enqueue(pkg);
        }


        /// <summary>
        /// 事件 Update
        /// 上层收到 OnWakeup 后调度本方法; 空转无害.
        /// </summary>
        public void Update()
        {
            while (stateQue.TryDequeue(out var sc))
            {
                OnStateChanged?.Invoke(sc.prev, sc.cur);
            }

            int n;
            do
            {
                n = rcvQue.Wait(rcvBatch, 0);   // timeout=0: 非阻塞取一批
                for (int i = 0; i < n; i++)
                {
                    var pk = rcvBatch[i];
                    OnPackage?.Invoke(pk);
                    Package.Pool.Return(pk);
                }
            } while (n > 0);
        }


        /// <summary>
        /// 拉起 ioSend / ioRecv 线程(幂等)
        /// </summary>
        private void Start()
        {
            if (Interlocked.CompareExchange(ref running, 1, 0) != 0)
            {
                return;
            }

            sndThread = new Thread(SndLoop) { IsBackground = true, Name = "hydra-snd" };
            rcvThread = new Thread(RcvLoop) { IsBackground = true, Name = "hydra-rcv" };
            sndThread.Start();
            rcvThread.Start();
        }


        /// <summary>
        /// ioSend 线程
        /// </summary>
        private void SndLoop()
        {
            var pks = new Package[SND_BATCH];
            while (Volatile.Read(ref running) == 1)
            {
                int n = sndQue.Wait(pks, TICK_MS);   // 超时兼作 tick 周期
                for (int i = 0; i < n; i++)
                {
                    if (KcpSession.Instance.Send(pks[i]) < 0)
                    {
                        Log.Write($"[Hydra] 发送失败: {pks[i]}");
                    }
                    Package.Pool.Return(pks[i]);
                }

                if (Volatile.Read(ref reconnecting) == 1)
                {// 重连中: 握手期的 KCP 由 WaitRegistRsp 自己驱动, 这里不 tick 也不判死
                    continue;
                }

                int res = KcpSession.Instance.Update((uint)Environment.TickCount);
                if (res == -4)
                {
                    KickCode = KcpSession.Instance.KickCode;
                    Log.Write($"[Hydra] 被服务端踢除: code = {KickCode}, 不再重连");
                    Close();
                    continue;
                }

                if (res < 0 && res != -1000)
                {// -1 超时 / -2 RST / -3 IO 判死
                    Log.Write($"[Hydra] KCP 判死: {res}, 发起重连");
                    KcpSession.Instance.Close();
                    TryReconnect();
                }
            }

            Log.Write("[Hydra] SndLoop 退出");
        }


        /// <summary>
        /// ioRecv 线程.
        /// </summary>
        private void RcvLoop()
        {
            var list = new LinkedList<Package>();
            while (Volatile.Read(ref running) == 1)
            {
                if (Volatile.Read(ref reconnecting) == 1)
                {// 重连中: REGIST_RSP 归 WaitRegistRsp 收, 这里必须让路, 否则会抢走握手包
                    Thread.Sleep(IDLE_MS);
                    continue;
                }

                if (KcpSession.Instance.Recv(ref list) < 0)
                {// -1000 会话已关(等重连/已停) / 其余网络错误(判死交给 SndLoop 的 Update) -> 退避
                    Thread.Sleep(IDLE_MS);
                    continue;
                }

                if (list.Count > 0)
                {
                    foreach (var pk in list)
                    {
                        rcvQue.Enqueue(pk);
                    }
                    list.Clear();
                    OnWakeup?.Invoke();
                }
            }

            Log.Write("[Hydra] RcvLoop 退出");
        }


        private void StartPing()
        {
            StopPing();
            pingTimer = new Timer(OnPing, null, PING_MS, PING_MS);
        }


        private void StopPing()
        {
            pingTimer?.Dispose();
            pingTimer = null;
        }


        private async void OnPing(object? state)
        {
            try
            {
                if (await Ping.POST() == 0)
                {
                    return;
                }

                if (Volatile.Read(ref reconnecting) == 0)
                {
                    await Refresh.POST();
                }
            }
            catch (Exception ex)
            {
                Log.Write($"[Hydra] 会话保活失败: {ex.Message}");
            }
        }


        /// <summary>
        /// 发起重连(幂等): 只允许一个 Reconnect 在跑
        /// </summary>
        private void TryReconnect()
        {
            if (Interlocked.CompareExchange(ref reconnecting, 1, 0) != 0)
            {
                return;
            }

            KickCode = 0;   // 新一轮连接: 清掉上次的踢人原因
            SetState(HydraState.Reconnecting);
            _ = Reconnect();
        }


        /// <summary>
        /// 重连: 周期性 RA /refresh(换新 conv/token) + KCP Open, 直到成功或超出总预算.
        /// 成功 -> Connected; 预算耗尽 -> Disconnected(上层应回登录页).
        /// </summary>
        private async Task Reconnect()
        {
            var sw = Stopwatch.StartNew();
            try
            {
                while (Volatile.Read(ref running) == 1 && sw.ElapsedMilliseconds < reconnectMaxMs)
                {
                    try
                    {
                        await Refresh.POST().ConfigureAwait(false);
                        if (await KcpSession.Instance.Open().ConfigureAwait(false) == 0)
                        {
                            SetState(HydraState.Connected);
                            return;
                        }
                    }
                    catch (Exception ex)
                    {
                        Log.Write($"[Hydra] 重连尝试失败: {ex.Message}");
                    }

                    await Task.Delay((int)reconnectRetryIntervalMs).ConfigureAwait(false);
                }

                Close();
            }
            finally
            {
                Interlocked.Exchange(ref reconnecting, 0);
            }
        }


        /// <summary>
        /// 设置 Hydra 状态: 状态变化入队, 经 OnWakeup -> Update() 在上层线程派发
        /// </summary>
        /// <param name="s"></param>
        private void SetState(HydraState s)
        {
            if (State == s)
                return;

            var prev = State;
            State = s;
            stateQue.Enqueue((prev, s));
            OnWakeup?.Invoke();
        }


        /// <summary>
        /// 把队列里滞留的包全部归还对象池(Close 收尾用)
        /// </summary>
        private static void DrainToPool(BlockingQueue<Package> que)
        {
            var batch = new Package[RCV_BATCH];
            int n;
            while ((n = que.Wait(batch, 0)) > 0)
            {
                for (int i = 0; i < n; i++)
                {
                    Package.Pool.Return(batch[i]);
                }
            }
        }


        // 收发线程开关: 1 运行中 / 0 停止
        private int running;

        // 是否正在重连: 1 重连中(RcvLoop 让路, SndLoop 停 tick)
        private int reconnecting;

        // 重连总预算(毫秒), 超过 -> Disconnected
        private uint reconnectMaxMs = 300000;

        // 两次重连尝试之间的间隔(毫秒)
        private uint reconnectRetryIntervalMs = 5000;

        // HTTP 会话保活定时器(登录起、Close 停)
        private Timer? pingTimer;

        // 状态变化事件(上层线程派发)
        private Action<HydraState, HydraState>? OnStateChanged;

        // 收包事件(上层线程派发)
        private Action<Package>? OnPackage;

        // 唤醒事件(任意线程触发, 上层负责把 Update 调度回自己线程)
        private Action? OnWakeup;

        // ioSend / ioRecv 线程
        private Thread? sndThread;
        private Thread? rcvThread;

        // 待发队列(上层 -> ioSend), 队列里的包所有权归 Hydra
        private readonly BlockingQueue<Package> sndQue = new BlockingQueue<Package>();

        // 已收队列(ioRecv -> 上层), Update 派发后归还对象池
        private readonly BlockingQueue<Package> rcvQue = new BlockingQueue<Package>();

        // 状态变化队列(任意线程 -> 上层)
        private readonly ConcurrentQueue<(HydraState prev, HydraState cur)> stateQue
            = new ConcurrentQueue<(HydraState, HydraState)>();

        // Update 派发用的批缓冲(仅上层线程碰)
        private readonly Package[] rcvBatch = new Package[RCV_BATCH];
    }
}
