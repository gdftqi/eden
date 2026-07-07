using Avalonia;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Layout;
using Avalonia.Media;
using System;

namespace CC
{
    // 联系人列表面板: 标题 + 搜索栏 + 添加联系人 + 列表; 选中联系人抛 ContactSelected 给宿主
    public partial class ContactListPanel : UserControl
    {
        // 选中某个联系人时触发(宿主据此打开右侧详情)
        public event Action<string>? ContactSelected;

        // 点了"添加联系人"时触发(宿主决定弹窗还是切界面)
        public event Action? AddContactRequested;

        public ContactListPanel()
        {
            InitializeComponent();
            LoadSampleContacts();
        }

        // 临时: 示例联系人(以后换成真实数据)
        private void LoadSampleContacts()
        {
            var avatar = new Avalonia.Media.Imaging.Bitmap(
                Avalonia.Platform.AssetLoader.Open(new Uri("avares://CC/Resources/unnamed.jpg")));

            (string nick, string sign)[] data =
            {
                ("利群", "低调的奢华"),
                ("xinfeng", "紫色的玫瑰"),
                ("美女1", "今天也要加油鸭"),
                ("美女2", ""),
                ("美女3", "忙, 勿扰"),
            };

            foreach (var (nick, sign) in data)
            {
                AddContact(avatar, nick, sign);
            }

            for (int i = 1; i <= 15; i++)
            {
                AddContact(avatar, $"联系人 {i}", "这是一条示例签名");
            }
        }

        // 建一行联系人: [头像] 昵称/签名
        private void AddContact(IImage avatar, string nick, string sign)
        {
            var face = new Border
            {
                Width = 44, Height = 44, CornerRadius = new CornerRadius(22), ClipToBounds = true,
                Child = new Image { Source = avatar, Stretch = Stretch.UniformToFill },
            };

            var lines = new StackPanel
            {
                VerticalAlignment = VerticalAlignment.Center, Spacing = 3, Margin = new Thickness(12, 0, 0, 0),
            };
            lines.Children.Add(new TextBlock { Text = nick, FontSize = 14.5, Foreground = new SolidColorBrush(Color.Parse("#1E1E1E")) });
            if (!string.IsNullOrEmpty(sign))
            {
                lines.Children.Add(new TextBlock { Text = sign, FontSize = 12, Foreground = new SolidColorBrush(Color.Parse("#8A8A8A")) });
            }

            var body = new StackPanel { Orientation = Orientation.Horizontal };
            body.Children.Add(face);
            body.Children.Add(lines);

            var row = new Border
            {
                Classes = { "contact" },
                Height = 64,
                Padding = new Thickness(14, 0),
                Child = body,
            };
            row.PointerPressed += (_, _) => ContactSelected?.Invoke(nick);
            ContactList.Children.Add(row);
        }

        private void AddContact_Click(object? sender, RoutedEventArgs e)
        {
            AddContactRequested?.Invoke();
        }

        private void ClearSearch_Click(object? sender, RoutedEventArgs e)
        {// 搜索框 清空文字
            SearchBox.Text = string.Empty;
            SearchBox.Focus();
        }
    }
}
