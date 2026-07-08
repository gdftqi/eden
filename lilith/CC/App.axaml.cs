using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Markup.Xaml;
using Avalonia.Threading;
using Lilith.Components;

namespace CC
{
    public partial class App : Application
    {
        public override void Initialize()
        {
            AvaloniaXamlLoader.Load(this);
        }

        public override void OnFrameworkInitializationCompleted()
        {
            if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
            {
                // 配置 Hydra(RA 地址/公钥、超时、重连预算由宿主注入)+ 接上 pump:
                // KCP IO 线程唤醒 → 这里 marshal 到 UI 线程后调 Hydra.Update, 让所有回调落在 UI 线程。
                Hydra.Instance
                    .SetHttpBaseUrl(Config.HTTP_HOST)
                    .SetHttpX25519PK(Config.HTTP_X25519_PK)
                    .SetKcpTimeoutMs(Config.KCP_TIMEOUT)
                    .SetReconnectMaxMs(Config.RECONNECT_MAX_TIME)
                    .SetonWakeup(() => Dispatcher.UIThread.Post(Hydra.Instance.Update));

                desktop.MainWindow = new LoginWindow();
            }

            base.OnFrameworkInitializationCompleted();
        }
    }
}