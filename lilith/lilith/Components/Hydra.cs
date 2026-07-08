using Lilith.Core;
using Lilith.Core.Ra;
using Lilith.Utils;
using System;
using System.Diagnostics;
using System.Net;
using System.Threading.Tasks;

namespace Lilith.Components
{
    // Hydra 会话状态(暴露给宿主应用)
    public enum HydraState
    {
        Disconnected,  // 未连接 / 需重新登录
        Connecting,    // 首次: RA 登录 + KCP 握手中
        Connected,     // 已连上, 可收发业务
        Reconnecting,  // 掉线, Hydra 正在 RA 刷新 + 重连(预算内)
    }


    public class Hydra: ISessionEvent
    {
        static readonly Hydra instance = new Hydra();
        public static Hydra Instance { get { return instance; } }

        private Hydra()
        {
            KcpSession.Instance.SetEvent(this);
            KcpSession.Instance.OnWakeup = () => OnWakeup?.Invoke();
        }


        public Hydra SetHttpBaseUrl(string url)
        {
            HttpSession.Instance.SetBaseUrl(url);
            return this;
        }


        public Hydra SetHttpX25519PK(string b64X25519PK)
        {
            HttpSession.Instance.SetHttpX25519PK(b64X25519PK);
            return this;
        }


        public Hydra SetKcpTimeoutMs(uint timeoutMs)
        {
            kcpTimeoutMs = timeoutMs;
            return this;
        }


        public Hydra SetReconnectMaxMs(uint maxMs)
        {
            reconnectMaxMs = maxMs;
            return this;
        }


        public Hydra SetReconnectRetryIntervalMs(uint ms)
        {
            reconnectRetryIntervalMs = ms;
            return this;
        }


        public Hydra SetOnStateChanged(Action<HydraState, HydraState> handler)
        {
            OnStateChanged = handler;
            return this;
        }


        public Hydra SetOnPackage(Action<Package> handler)
        {
            OnPackage = handler;
            return this;
        }


        public Hydra SetonWakeup(Action handler)
        {
            OnWakeup = handler;
            return this;
        }

        public HydraState State { get; private set; } = HydraState.Disconnected;

        public void Update()
        {
            KcpSession.Instance.Update();
        }


        public async Task<bool> Login(string username, string password)
        {
            SetState(HydraState.Connecting);
            try
            {
                await UserLogin.POST(username, password);
                bool ok = await KcpSession.Instance.Connect(kcpTimeoutMs);
                if (!ok)
                {
                    SetState(HydraState.Disconnected);
                }
                return ok;
            }
            catch (Exception ex)
            {
                FileLog.Write($"[Hydra] Login 失败: {ex.Message}");
                SetState(HydraState.Disconnected);
                return false;
            }
        }

        public void Send(Package pkg)
        {
            KcpSession.Instance.Send(pkg);
        }


        public void Close()
        {
            SetState(HydraState.Disconnected);
            KcpSession.Instance.Close();
        }


        void ISessionEvent.OnConnected(EndPoint host)
        {
            reconnecting = false;
            SetState(HydraState.Connected);
        }

        void ISessionEvent.OnDisconnected(EndPoint host)
        {
            var reason = KcpSession.Instance.DeadReason;
            FileLog.Write($"[Hydra] OnDisconnected: reason={reason}, state={State}, reconnecting={reconnecting}");

            if (reconnecting)
            {
                return;
            }

            if (reason == DisconnectReason.None)
            {
                return;
            }

            if (State != HydraState.Connected)
            {
                return;
            }

            _ = Reconnect();
        }

        void ISessionEvent.OnPackage(Package pkg)
        {
            OnPackage?.Invoke(pkg);
        }

        // ================= 重连循环(Hydra 自己完成: RA 刷新 + KCP 重连) =================

        private async Task Reconnect()
        {
            reconnecting = true;
            SetState(HydraState.Reconnecting);

            var sw = Stopwatch.StartNew();
            try
            {
                while (sw.ElapsedMilliseconds < reconnectMaxMs)
                {
                    try
                    {
                        await Refresh.POST();
                        if (await KcpSession.Instance.Connect(kcpTimeoutMs))
                        {
                            return;
                        }
                    }
                    catch (Exception ex)
                    {
                        FileLog.Write($"[Hydra] 重连尝试失败: {ex.Message}");
                    }

                    await Task.Delay((int)reconnectRetryIntervalMs);
                }

                SetState(HydraState.Disconnected);   // 预算耗尽 → 需重新登录
            }
            finally
            {
                reconnecting = false;
            }
        }

        private void SetState(HydraState s)
        {
            if (State == s)
            {
                return;
            }
            var prev = State;
            State = s;
            OnStateChanged?.Invoke(prev, s);   // (之前状态, 当前状态)
        }

        private uint kcpTimeoutMs { get; set; } = 30000;             // 单次 Connect 的 KCP 超时
        private uint reconnectMaxMs { get; set; } = 300000;           // 重连总预算, 超过 → Disconnected
        private uint reconnectRetryIntervalMs { get; set; } = 2000;   // 两次重连尝试之间的间隔

        private Action<HydraState, HydraState>? OnStateChanged;   // (之前状态, 当前状态)
        private Action<Package>? OnPackage;
        private Action? OnWakeup;

        private bool reconnecting;
    }
}
