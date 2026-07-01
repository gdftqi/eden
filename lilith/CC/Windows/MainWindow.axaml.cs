using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Threading;
using lilith.Core;
using System;
using System.Diagnostics;
using System.Net;

namespace CC
{
    public partial class MainWindow : Window, ISessionEvent
    {
        // ---- KCP echo 测试参数 ----
        const ushort ECHO_PKID = 1;

        public MainWindow()
        {
            InitializeComponent();
            ChatPanel.ChatSelected += OpenChat;
            ChatView.SendRequested += OnSendText;
            KcpSession.Instance.OnWakeup = () => Dispatcher.UIThread.Post(KcpSession.Instance.Update);
            this.Opened += (_, __) => DemoConnBanner();   // [DEMO] 看完删这行 + 下面的 DemoConnBanner
        }

        // 选中某个会话(由 ChatListPanel.ChatSelected 触发): 顶栏换成该用户, 显示 ChatWindow(隐藏空态)
        private void OpenChat(ChatItem item)
        {
            ChatView.PeerName = item.Nickname;
            ChatView.PeerAvatar = item.Avatar;
            ChatView.PeerStatus = "最后在线 今天 17:20";

            // 临时: 每个会话都用同一组示例消息(以后换成各自的真实消息)
            ChatView.ClearMessages();
            EmptyState.IsVisible = false;
            ChatView.IsVisible = true;
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
            // TODO: 打开设置面板
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
            // TODO: 切换到聊天
        }

        private void Contacts_Click(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
        {
            SetNav(ContactsIcon);
            // TODO: 切换到联系人列表
        }

        private void Org_Click(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
        {
            SetNav(OrgIcon);
            // TODO: 切换到组织架构
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
            Debug.WriteLine("连接 {0} 成功", host);
            ShowConnState(ConnState.Connected);
        }

        public void OnDisconnected(EndPoint host)
        {
            Debug.WriteLine("连接 {0} 断开", host);
            ShowConnState(ConnState.Disconnected);
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

        // 收起提示条(滑回左边藏起来)
        public void HideConnBanner()
        {
            ConnBanner.RenderTransform = Avalonia.Media.Transformation.TransformOperations.Parse("translateX(-180px)");
        }

        // Connected 后停 1.5s 自动收起; 期间若又切了别的状态(gen 变了)则跳过, 不误收
        private async void AutoHideConnBanner(int gen)
        {
            await System.Threading.Tasks.Task.Delay(1500);
            if (gen == connStateGen)
            {
                HideConnBanner();
            }
        }

        // [DEMO] 临时: 启动后循环演示效果, 看完把这个方法 + ctor 里的 Opened 订阅删掉
        private async void DemoConnBanner()
        {
            while (true)
            {
                await System.Threading.Tasks.Task.Delay(1500);
                ShowConnState(ConnState.Disconnected);   // 断开 → 红点
                await System.Threading.Tasks.Task.Delay(2500);
                ShowConnState(ConnState.Reconnecting);   // 重连 → 转圈
                await System.Threading.Tasks.Task.Delay(3000);
                ShowConnState(ConnState.Connected);      // 成功 → 绿点, 1.5s 后自动收起
                await System.Threading.Tasks.Task.Delay(2500);   // 等它收起 + 停顿再下一轮
            }
        }

        // 服务端 echo 回来(主线程回调): 显示成"对方"的消息
        public void OnPackage(Package pkg)
        {
            var text = System.Text.Encoding.UTF8.GetString(pkg.Payload, 0, pkg.PayloadLength);
            ChatView.AddText(false, text, DateTime.Now.ToString("HH:mm"));
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