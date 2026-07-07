using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Threading;
using lilith.Core;
using lilith.Tools;
using System;
using System.Diagnostics;
using System.Net;
using System.Threading.Tasks;

namespace CC
{
    public partial class MainWindow : Window, ISessionEvent
    {
        // ---- KCP echo 测试参数 ----
        const ushort ECHO_PKID = 1;

        public MainWindow()
        {
            InitializeComponent();
            TabChat.SendRequested += OnSendText;
            KcpSession.Instance.OnWakeup = () => Dispatcher.UIThread.Post(KcpSession.Instance.Update);
        }


        // 切换页签: 叠放的四个 Tab 只显示一个
        private void ShowTab(Control tab)
        {
            TabChat.IsVisible = tab == TabChat;
            TabContact.IsVisible = tab == TabContact;
            TabOrg.IsVisible = tab == TabOrg;
            TabSettings.IsVisible = tab == TabSettings;
        }

        private void Button_Click(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
        {
            if (Application.Current?.ApplicationLifetime is IControlledApplicationLifetime lifetime)
            {
                lifetime.Shutdown();
            }
        }

        private void Minimize_Click(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
        {
            WindowState = WindowState.Minimized;
        }

        private void Settings_Click(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
        {
            SetNav(SettingsIcon);
            ShowTab(TabSettings);
        }

        private static readonly Avalonia.Media.IBrush ActiveIcon = new Avalonia.Media.SolidColorBrush(Avalonia.Media.Color.Parse("#66BB6A"));
        private static readonly Avalonia.Media.IBrush InactiveIcon = Avalonia.Media.Brushes.White;

        private void SetNav(Avalonia.Controls.Shapes.Path active)
        {
            ChatIcon.Fill = active == ChatIcon ? ActiveIcon : InactiveIcon;
            ContactsIcon.Fill = active == ContactsIcon ? ActiveIcon : InactiveIcon;
            OrgIcon.Fill = active == OrgIcon ? ActiveIcon : InactiveIcon;
            SettingsIcon.Fill = active == SettingsIcon ? ActiveIcon : InactiveIcon;
        }

        private void Chat_Click(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
        {
            SetNav(ChatIcon);
            ShowTab(TabChat);
        }

        private void Contacts_Click(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
        {
            SetNav(ContactsIcon);
            ShowTab(TabContact);
        }

        private void Org_Click(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
        {
            SetNav(OrgIcon);
            ShowTab(TabOrg);
        }

        private void ToggleMaximize()
        {
            bool toMax = WindowState != WindowState.Maximized;
            WindowState = toMax ? WindowState.Maximized : WindowState.Normal;
            MaxIcon.Data = Avalonia.Media.Geometry.Parse(toMax ? "M3,7 H11 V14 H3 Z M6,7 V4 H14 V12 H11" : "M1,1 H15 V15 H1 Z");
        }

        private void Maximize_Click(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
        {
            ToggleMaximize();
        }

        private void TopBar_PointerPressed(object? sender, Avalonia.Input.PointerPressedEventArgs e)
        {
            if (e.GetCurrentPoint(this).Properties.IsLeftButtonPressed)
            {
                if (e.ClickCount == 2)
                {
                    ToggleMaximize();
                }
                else
                {
                    this.BeginMoveDrag(e);
                }
            }
        }

        private void Resize_PointerPressed(object? sender, Avalonia.Input.PointerPressedEventArgs e)
        {
            if (sender is Control c && c.Tag is string edge && e.GetCurrentPoint(this).Properties.IsLeftButtonPressed)
            {
                this.BeginResizeDrag(System.Enum.Parse<WindowEdge>(edge), e);
            }
        }

        public void OnConnected(EndPoint host)
        {
            FileLog.Write($"[MW] OnConnected: {host}");
            ShowConnState(ConnState.Connected);
        }

        public void OnDisconnected(EndPoint host)
        {
            FileLog.Write($"[MW] OnDisconnected: {host}, DeadReason={KcpSession.Instance.DeadReason}, reconnecting(当前)={reconnecting}");

            if (reconnecting)
            {
                FileLog.Write("[MW] OnDisconnected: reconnecting 已为 true, 忽略这次重入");
                return;
            }

            if (KcpSession.Instance.DeadReason == DisconnectReason.None)
            {
                HideConnBanner();
                return;
            }

            _ = Reconnect();
        }

        private bool reconnecting;

        const int RECONNECT_RETRY_INTERVAL_MS = 2000;

        private async Task Reconnect()
        {
            FileLog.Write($"[MW] Reconnect() 开始");
            reconnecting = true;
            ShowConnState(ConnState.Reconnecting);

            var sw = Stopwatch.StartNew();
            int attempt = 0;
            try
            {
                while (sw.ElapsedMilliseconds < Config.RECONNECT_MAX_TIME)
                {
                    attempt++;
                    FileLog.Write($"[MW] Reconnect() 第 {attempt} 次尝试, 已耗时={sw.ElapsedMilliseconds}ms");
                    try
                    {
                        await Proxy.Refresh.POST();
                        FileLog.Write($"[MW] Reconnect() Refresh.POST 返回, 即将 Connect(), 已耗时={sw.ElapsedMilliseconds}ms");

                        bool ok = await KcpSession.Instance.Connect(this, Config.KCP_TIMEOUT);
                        FileLog.Write($"[MW] Reconnect() Connect() 返回 {ok}, 已耗时={sw.ElapsedMilliseconds}ms");

                        if (ok)
                        {
                            return;
                        }
                    }
                    catch (Exception ex)
                    {
                        FileLog.Write($"[MW] Reconnect() 第 {attempt} 次尝试失败: {ex}");
                    }

                    await Task.Delay(RECONNECT_RETRY_INTERVAL_MS);
                }

                FileLog.Write($"[MW] Reconnect() 超过 {Config.RECONNECT_MAX_TIME}ms 仍未连上, 回登录页");
                BackToLogin();
            }
            finally
            {
                FileLog.Write($"[MW] Reconnect() 结束, reconnecting 置回 false");
                reconnecting = false;
            }
        }

        private void BackToLogin()
        {
            FileLog.Write($"[MW] BackToLogin() 调用");
            KcpSession.Instance.Close();

            var login = new LoginWindow();
            if (Application.Current?.ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
            {
                desktop.MainWindow = login;
            }
            login.Show();
            Close();
        }

        // ============ 连接状态提示条 ============

        public enum ConnState { Disconnected, Reconnecting, Connected }

        private static readonly Avalonia.Media.IBrush DotRed = new Avalonia.Media.SolidColorBrush(Avalonia.Media.Color.Parse("#E53935"));
        private static readonly Avalonia.Media.IBrush DotGreen = new Avalonia.Media.SolidColorBrush(Avalonia.Media.Color.Parse("#43A047"));
        private int connStateGen;   // 状态代次: Connected 的 1.5s 自动收起用它防"期间又切了别的状态时误收"

        // 显示提示条并滑出
        public void ShowConnState(ConnState state)
        {
            int gen = ++connStateGen;

            switch (state)
            {
                case ConnState.Disconnected:
                    ConnText.Text = "连接断开";
                    ConnDot.Fill = DotRed;
                    ConnDot.IsVisible = true;
                    ConnSpinner.IsVisible = false;
                    break;

                case ConnState.Reconnecting:
                    ConnText.Text = "重新连接";
                    ConnDot.IsVisible = false;
                    ConnSpinner.IsVisible = true;
                    break;

                case ConnState.Connected:
                    ConnText.Text = "连接成功";
                    ConnDot.Fill = DotGreen;
                    ConnDot.IsVisible = true;
                    ConnSpinner.IsVisible = false;
                    break;
            }

            ConnBanner.RenderTransform = Avalonia.Media.Transformation.TransformOperations.Parse("translateX(0px)");

            if (state == ConnState.Connected)
            {
                AutoHideConnBanner(gen);   // 连接成功: 停留 1.5s 后自动收起
            }
        }


        public void HideConnBanner()
        {
            ConnBanner.RenderTransform = Avalonia.Media.Transformation.TransformOperations.Parse("translateX(-180px)");
        }


        private async void AutoHideConnBanner(int gen)
        {
            await Task.Delay(1500);
            if (gen == connStateGen)
            {
                HideConnBanner();
            }
        }

        // 服务端 echo 回来(主线程回调)
        public void OnPackage(Package pkg)
        {
            var text = System.Text.Encoding.UTF8.GetString(pkg.Payload, 0, pkg.PayloadLength);
            TabChat.AddText(false, text, DateTime.Now.ToString("HH:mm"));
        }

        // ChatWindow 发送时触发: 把文字打包成业务 echo 包发给服务端
        private void OnSendText(string text)
        {
            if (!KcpSession.Instance.Running)
            {
                return;
            }

            var pkg = Package.Pool.Take();
            pkg.ID = ECHO_PKID;
            pkg.DstID = 10000;
            pkg.PayloadLength = System.Text.Encoding.UTF8.GetBytes(text, 0, text.Length, pkg.Payload, 0);
            KcpSession.Instance.Send(pkg);
            Package.Pool.Return(pkg);
        }
    }
}