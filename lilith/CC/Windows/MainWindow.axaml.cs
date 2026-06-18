using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;

namespace CC
{
    public partial class MainWindow : Window
    {
        public MainWindow()
        {
            InitializeComponent();
            LoadSampleChats();
            // 刚登录未选会话: 显示空态(Ozymandias), 不显示 ChatWindow
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
            (bool mine, string text, string time, int status)[] msgs =
            {
                (false, "滴滴滴",               "17:15", 0),
                (true,  "在",                   "17:15", 3),
                (false, "11楼",                 "17:16", 0),
                (true,  "1",                    "17:16", 3),
                (true,  "我等电梯",             "17:18", 3),
                (true,  "你找个人帮我按一下",   "17:18", 3),
                (true,  "你说可以进我就进电梯", "17:18", 3),
                (false, "1",                    "17:18", 0),
                (false, "来",                   "17:20", 0),
                (true,  "还没进，下一趟就进",   "17:20", 3),
            };
            foreach (var (mine, text, time, status) in msgs)
                ChatView.AddText(mine, text, time, status);

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
    }
}