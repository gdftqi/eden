using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Lilith.Components;
using Lilith.Core;
using System;
using System.Threading.Tasks;

namespace CC
{
    public partial class MainWindow : Window
    {
        // ---- KCP echo 测试参数 ----
        const ushort ECHO_PKID = 1;

        public MainWindow()
        {
            InitializeComponent();
            TabChat.SendRequested += OnSendText;
            // 主窗接管 Hydra 的高层回调(pump 在 App 里已接好, 这里不用管)
            Hydra.Instance
                .SetOnStateChanged(OnHydraState)
                .SetOnPackage(OnPackage);
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

        // Hydra 状态变化(主线程回调): 驱动浮窗 / 掉线回登录
        private void OnHydraState(HydraState prev, HydraState cur)
        {
            switch (cur)
            {
                case HydraState.Reconnecting:
                    ShowConnState(ConnState.Reconnecting);
                    break;

                case HydraState.Connected:
                    // 只在"重连成功"时弹绿点; 首次登录连上不弹(登录流程已处理)
                    if (prev == HydraState.Reconnecting)
                    {
                        ShowConnState(ConnState.Connected);
                    }
                    break;

                case HydraState.Disconnected:
                    BackToLogin();   // 重连放弃 / 会话失效 → 回登录页
                    break;
            }
        }

        private void BackToLogin()
        {
            // 会话已由 Hydra 关闭; 这里只做窗口切换。新 LoginWindow 会接管 Hydra 回调, 本窗回调随之失效。
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

        // 服务端 echo 回来(Hydra 主线程回调)
        private void OnPackage(Package pkg)
        {
            var text = System.Text.Encoding.UTF8.GetString(pkg.Payload, 0, pkg.PayloadLength);
            TabChat.AddText(false, text, DateTime.Now.ToString("HH:mm"));
        }

        // ChatWindow 发送时触发: 把文字打包成业务 echo 包发给服务端
        private void OnSendText(string text)
        {
            if (Hydra.Instance.State != HydraState.Connected)
            {
                return;
            }

            var pkg = Package.Pool.Take();
            pkg.ID = ECHO_PKID;
            pkg.DstID = 10000;
            pkg.PayloadLength = System.Text.Encoding.UTF8.GetBytes(text, 0, text.Length, pkg.Payload, 0);
            Hydra.Instance.Send(pkg);
            Package.Pool.Return(pkg);
        }
    }
}