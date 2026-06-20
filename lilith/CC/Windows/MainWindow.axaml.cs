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
            // 事件驱动: IO 线程有事件时通知, 切到 UI 线程跑一次 Update(替代轮询定时器)
            KcpSession.Instance.OnEventQueued = () => Dispatcher.UIThread.Post(KcpSession.Instance.Update);
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
        }

        public void OnDisconnected(EndPoint host)
        {
            Debug.WriteLine("连接 {0} 断开", host);
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
            pkg.PkId = ECHO_PKID;
            pkg.PkDstId = 10000;
            pkg.PayloadLength = System.Text.Encoding.UTF8.GetBytes(text, 0, text.Length, pkg.Payload, 0);
            KcpSession.Instance.Send(pkg);
            Package.Pool.Return(pkg);
        }
    }
}