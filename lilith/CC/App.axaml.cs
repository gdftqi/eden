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
            /// HTTP 服务的 X25519 公钥
            const string HTTP_X25519_PK = "LZBT82+6Hdzz/pqnOyk3tRh1460vhxVJ1NcvLT3kn0M=";

            /// HTTP 服务地址
            //public const string HTTP_HOST = "http://172.26.29.158:8080";
            const string HTTP_HOST = "http://13.212.159.179:8080";

            /// KCP 连接超时
            const uint KCP_TIMEOUT = 30000;

            /// 重连的最大时间
            const uint RECONNECT_MAX_TIME = 300000;

            if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
            {
                Hydra.Instance
                    .SetHttpBaseUrl(HTTP_HOST)
                    .SetHttpX25519PK(HTTP_X25519_PK)
                    .SetKcpTimeoutMs(KCP_TIMEOUT)
                    .SetReconnectMaxMs(RECONNECT_MAX_TIME)
                    .SetonWakeup(() => Dispatcher.UIThread.Post(Hydra.Instance.Update));

                desktop.MainWindow = new LoginWindow();
            }

            base.OnFrameworkInitializationCompleted();
        }
    }
}