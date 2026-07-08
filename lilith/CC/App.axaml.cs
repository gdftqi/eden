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
                Hydra.Instance
                    // HTTP 服务地址
                    .SetHttpBaseUrl("http://13.212.159.179:8080")
                    // HTTP 服务的 X25519 公钥
                    .SetHttpX25519PK("LZBT82+6Hdzz/pqnOyk3tRh1460vhxVJ1NcvLT3kn0M=")
                    // KCP 连接超时
                    .SetKcpTimeout(30)
                    // 重连的最大时间
                    .SetReconnectMax(300)
                    // 设置 Update 回调
                    .SetOnWakeup(() => Dispatcher.UIThread.Post(Hydra.Instance.Update));

                desktop.MainWindow = new LoginWindow();
            }


            base.OnFrameworkInitializationCompleted();
        }
    }
}