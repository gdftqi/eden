using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using CC.Model;
using Lilith.Components;
using Lilith.Core;
using Lilith.Utils;
using System;
using System.Threading.Tasks;

namespace CC
{
    public partial class MainWindow : Window
    {
        // ---- KCP echo 测试参数 ----
        // 必须与 Noah 的 regist_handler(PID_CUSTOM + 1) 一致, 否则网关转发过滤会判死连接
        const ushort ECHO_PID = (ushort)(Package.PID_CUSTOM + 1);

        public MainWindow()
        {
            InitializeComponent();
            RefreshAvatar();
            TabChat.SendRequested += OnSendText;
            // 主窗接管 Hydra 的高层回调(pump 在 App 里已接好, 这里不用管)
            Hydra.Instance.SetOnStateChanged(OnHydraState).SetOnPackage(OnPackage);
        }


        /// <summary>
        /// 按当前登录用户重刷左下角头像.在设置页改完头像后也要调一次.
        /// </summary>
        public void RefreshAvatar()
        {
            Avatars.Bind(Avatar, Me.User?.Avatar);
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
            OpenChatTab();
        }


        /// <summary>
        /// 切到聊天页.
        /// </summary>
        public void OpenChatTab()
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
                    // 重连放弃 / 会话失效 -> 回登录页
                    BackToLogin();
                    break;
            }
        }


        private bool leaving;

        // 公开出去是为了让页签也能触发
        public async void BackToLogin()
        {
            if (leaving)
            {
                return;
            }
            leaving = true;

            // 会话已由 Hydra 关闭
            uint code = Hydra.Instance.KickCode;
            if (code != 0)
            {
                Hide();

                await MessageBoxWindow.Alert("已下线", Package.ErrorText(code), "重新登录");
            }

            // 原因已经用提示框说过了, 不必再在登录页重复一遍
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

        // ============ 操作结果提示(Toast) ============

        private static readonly Avalonia.Media.Geometry ToastOkGlyph =
            Avalonia.Media.Geometry.Parse("M9 16.17L4.83 12l-1.42 1.41L9 19 21 7l-1.41-1.41z");
        private static readonly Avalonia.Media.Geometry ToastFailGlyph =
            Avalonia.Media.Geometry.Parse("M11 7h2v6h-2zm0 8h2v2h-2z");

        private static readonly Avalonia.Media.IBrush ToastOkBg = new Avalonia.Media.SolidColorBrush(Avalonia.Media.Color.Parse("#43A047"));
        private static readonly Avalonia.Media.IBrush ToastFailBg = new Avalonia.Media.SolidColorBrush(Avalonia.Media.Color.Parse("#E53935"));

        // 与 connStateGen 同样的用意: 连着来两条时, 后一条不该被前一条的定时器收掉
        private int toastGen;

        /// <summary>
        /// 顶部弹一条一闪即收的提示.用于"刚才那步成没成"这类事后反馈,
        /// 不打断操作 -- 需要用户点一下才继续的场合请用 MessageBoxWindow.
        /// </summary>
        public void ShowToast(string text, bool ok = true)
        {
            int gen = ++toastGen;

            ToastText.Text = text;
            ToastIcon.Background = ok ? ToastOkBg : ToastFailBg;
            ToastGlyph.Data = ok ? ToastOkGlyph : ToastFailGlyph;

            ToastBanner.Opacity = 1;
            ToastBanner.RenderTransform = Avalonia.Media.Transformation.TransformOperations.Parse("translateY(0px)");

            AutoHideToast(gen);
        }


        private async void AutoHideToast(int gen)
        {
            await Task.Delay(2200);

            // 期间又来了新的一条: 收起交给那一条负责, 这里撒手
            if (gen != toastGen)
            {
                return;
            }

            ToastBanner.Opacity = 0;
            ToastBanner.RenderTransform = Avalonia.Media.Transformation.TransformOperations.Parse("translateY(-60px)");
        }

        // 服务端 echo 回来(Hydra 主线程回调)
        private void OnPackage(Package pkg)
        {
            // 框架消息必须先分流: 它们的 payload 是二进制, 当文本解会显示成乱码
            if (pkg.PID == Package.PID_TER_ERROR)
            {
                if (Package.DecodeError(pkg, out uint code, out uint dstId, out uint pid))
                {
                    Log.Write($"[CC] 请求未送达: code = {code}, dst_id = {dstId}, pid = {pid}");
                    TabChat.AddText(false, Package.ErrorText(code), DateTime.Now.ToString("HH:mm"));
                }
                return;
            }

            var text = System.Text.Encoding.UTF8.GetString(pkg.Payload, 0, pkg.PayloadLength);
            TabChat.AddText(false, text, DateTime.Now.ToString("HH:mm"));
        }

        // 聊天窗发送时触发: 把文字打包成业务 echo 包发给服务端
        private void OnSendText(string text)
        {
            if (Hydra.Instance.State != HydraState.Connected)
            {
                return;
            }

            var pkg = Package.Pool.Take();
            pkg.PID = ECHO_PID;
            pkg.DstID = 0x10010000;
            pkg.PayloadLength = System.Text.Encoding.UTF8.GetBytes(text, 0, text.Length, pkg.Payload, 0);
            // 所有权移交 Hydra: ioSend 线程发完负责还池, 这里不能再碰 pkg
            Hydra.Instance.Send(pkg);
        }
    }
}
