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
        }

        // 临时: 往会话列表塞几条示例数据, 验证 ChatItem 组件效果(以后换成真实数据)
        private void LoadSampleChats()
        {
            var avatar = new Avalonia.Media.Imaging.Bitmap(
                Avalonia.Platform.AssetLoader.Open(new System.Uri("avares://CC/Resources/unnamed.jpg")));

            (string nick, string msg, string time)[] data =
            {
                ("利群",         "还没进，下一趟就进",            "星期一"),
                ("德美便利超市", "满满今天应该没空",              "星期日"),
                ("楚风",         "好的",                          "星期六"),
                ("和和",         "你已开启限时消息功能。此聊天中的新消息将在阅读后消失", "星期六"),
                ("xinfeng",      "[语音] 0:02",                   "星期六"),
                ("叛逆小猫",     "好",                            "星期六"),
                ("皮",           "咋了",                          "2026年6月11日"),
            };

            foreach (var (nick, msg, time) in data)
            {
                ChatList.Children.Add(new ChatItem
                {
                    Avatar = avatar,
                    Nickname = nick,
                    LastMessage = msg,
                    Time = time
                });
            }

            // 多补几条, 让列表溢出, 看垂直滚动条效果
            for (int i = 1; i <= 15; i++)
            {
                ChatList.Children.Add(new ChatItem
                {
                    Avatar = avatar,
                    Nickname = $"联系人 {i}",
                    LastMessage = "这是一条示例消息，用来撑高列表",
                    Time = "昨天"
                });
            }
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