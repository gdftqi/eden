using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Markup.Xaml;
using Avalonia.Platform;
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
                    // 鉴权成功后自动登记进入CCS
                    .SetEnterServs(new uint[] { Model.ChatProto.CCS_ID })
                    // 重连的最大时间
                    .SetReconnectMax(300)
                    // 设置 Update 回调
                    .SetOnWakeup(() => Dispatcher.UIThread.Post(Hydra.Instance.Update));

                desktop.MainWindow = new LoginWindow();
            }

            base.OnFrameworkInitializationCompleted();
        }


        // ---- 托盘 ----

        private void Tray_Clicked(object? sender, System.EventArgs e)
        {
            ShowMain();
        }


        private void TrayOpen_Click(object? sender, System.EventArgs e)
        {
            ShowMain();
        }


        // 真正退出
        private void TrayExit_Click(object? sender, System.EventArgs e)
        {
            if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
            {
                desktop.Shutdown();
            }
        }


        // 托盘图标的两个形态: 平时的, 和右上角带红点的
        private static Avalonia.Controls.TrayIcon? tray;
        private static WindowIcon? iconIdle;
        private static WindowIcon? iconDot;


        private static Avalonia.Controls.TrayIcon? Tray()
        {
            if (tray != null || Application.Current == null)
            {
                return tray;
            }

            var icons = Avalonia.Controls.TrayIcon.GetIcons(Application.Current);
            if (icons != null && icons.Count > 0)
            {
                tray     = icons[0];
                iconIdle = tray.Icon;   // XAML 里设的那张, 存下来好切回去
            }

            return tray;
        }


        /// <summary>
        /// 有没看过的消息时给托盘图标点个红点(窗口收在托盘里时的唯一提示).
        /// </summary>
        public static void SetTrayDot(bool on)
        {
            var t = Tray();
            if (t == null)
            {
                return;
            }

            if (!on)
            {
                if (iconIdle != null)
                {
                    t.Icon = iconIdle;
                }
                return;
            }

            if (iconDot == null)
            {
                try
                {
                    using var s = AssetLoader.Open(new System.Uri("avares://CC/Resources/app_dot.png"));
                    iconDot = new WindowIcon(s);
                }
                catch (System.Exception ex)
                {
                    Lilith.Utils.Log.Write($"[CC] 托盘红点图标加载失败: {ex.Message}");
                    return;
                }
            }

            t.Icon = iconDot;
        }


        public static void SetTrayUser(string? name)
        {
            if (Application.Current == null)
            {
                return;
            }

            var icons = Avalonia.Controls.TrayIcon.GetIcons(Application.Current);
            if (icons == null || icons.Count == 0)
            {
                return;
            }

            var items = icons[0].Menu?.Items;
            if (items != null && items.Count > 0 && items[0] is Avalonia.Controls.NativeMenuItem head)
            {
                head.Header = string.IsNullOrEmpty(name) ? "未登录" : name;
            }
        }


        private static void ShowMain()
        {
            if (Application.Current?.ApplicationLifetime is not IClassicDesktopStyleApplicationLifetime desktop
                || desktop.MainWindow == null)
            {
                return;
            }

            var w = desktop.MainWindow;
            w.Show();

            if (w.WindowState == Avalonia.Controls.WindowState.Minimized)
            {
                w.WindowState = Avalonia.Controls.WindowState.Normal;
            }

            w.Activate();
        }
    }
}