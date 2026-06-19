using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Threading;
using lilith.Core;
using lilith.Utils;
using System;
using System.Diagnostics;
using System.Net;

namespace CC
{
    public partial class MainWindow : Window, ISessionEvent
    {
        // ---- KCP echo 测试参数 ----
        const string SERVER    = "13.214.204.197:5555";
        const uint   CONV      = Crypto.CONV_BASE;   // 2000, 对应 TOKENS_B64[0]
        const ushort ECHO_PKID = 1;                  // 业务 echo 号 (= test_kcp.py PK_ID_PING)

        private readonly DispatcherTimer _kcpTimer;

        public MainWindow()
        {
            InitializeComponent();
            LoadSampleChats();
            // 刚登录未选会话: 显示空态(Ozymandias), 不显示 ChatWindow

            // 接 KCP: 会话事件统一在主线程回调(DispatcherTimer 周期 Update 投递)
            KcpSession.Instance.SetEvent(this);
            ChatView.SendRequested += OnSendText;
            _kcpTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(16) };
            _kcpTimer.Tick += (_, _) => KcpSession.Instance.Update();
            _kcpTimer.Start();
            try { KcpSession.Instance.Connect(CONV, SERVER); }
            catch (Exception ex) { Debug.WriteLine("KCP connect 失败: " + ex.Message); }
        }

        // 临时: 往会话列表塞几条示例数据, 验证 ChatItem 组件效果(以后换成真实数据)
        private void LoadSampleChats()
        {
            var avatar = new Avalonia.Media.Imaging.Bitmap(
                Avalonia.Platform.AssetLoader.Open(new System.Uri("avares://CC/Resources/unnamed.jpg")));

            (string nick, string msg, string time, int unread)[] data =
            {
                ("美女1", "111111", "星期一", 1),
                ("美女2", "222222", "星期二", 0),
                ("美女3", "333333", "星期三", 8),
                ("美女4", "444444", "星期四", 0),
                ("美女5", "555555", "星期五", 36),
                ("美女6", "666666", "星期六", 0),
                ("美女7", "777777", "2026年6月11日", 128),
            };

            foreach (var (nick, msg, time, unread) in data)
                AddChat(avatar, nick, msg, time, unread);

            // 多补几条, 让列表溢出, 看垂直滚动条效果
            for (int i = 1; i <= 15; i++)
                AddChat(avatar, $"联系人 {i}", "这是一条示例消息，用来撑高列表", "昨天");
        }

        // 建一个会话项, 接上"点击 = 选中并打开聊天"
        private void AddChat(Avalonia.Media.IImage avatar, string nick, string msg, string time, int unread = 0)
        {
            var item = new ChatItem { Avatar = avatar, Nickname = nick, LastMessage = msg, Time = time, Unread = unread };
            item.PointerPressed += (_, _) => OpenChat(item);
            ChatList.Children.Add(item);
        }

        // 选中某个会话: 顶栏换成该用户 + 载入消息, 显示 ChatWindow(隐藏空态)
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

        // 搜索框 × : 清空并重新聚焦(× 由绑定在有文字时显示)
        private void ClearSearch_Click(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
        {
            SearchBox.Text = string.Empty;
            SearchBox.Focus();
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
            if (!KcpSession.Instance.Running) return;

            var pkg = Package.Pool.Take();
            pkg.PkId          = ECHO_PKID;
            pkg.PkDstId       = Package.PK_DST_ID;
            pkg.PayloadLength = System.Text.Encoding.UTF8.GetBytes(text, 0, text.Length, pkg.Payload, 0);
            KcpSession.Instance.Send(pkg);
            Package.Pool.Return(pkg);
        }
    }
}