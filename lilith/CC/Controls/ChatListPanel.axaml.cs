using Avalonia.Controls;
using Avalonia.Interactivity;
using System;

namespace CC
{
    // 会话列表面板: 头部 + 搜索栏 + 用户列表; 选中某会话抛 ChatSelected 给宿主
    public partial class ChatListPanel : UserControl
    {
        // 选中某个会话时触发(宿主据此打开对应聊天窗)
        public event Action<ChatItem>? ChatSelected;

        public ChatListPanel()
        {
            InitializeComponent();
            LoadSampleChats();
        }

        // 临时: 往会话列表塞几条示例数据(以后换成真实数据)
        private void LoadSampleChats()
        {
            var avatar = new Avalonia.Media.Imaging.Bitmap(
                Avalonia.Platform.AssetLoader.Open(new Uri("avares://CC/Resources/unnamed.jpg")));

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

        // 建一个会话项, 点击 = 选中(focus 高亮) + 抛 ChatSelected
        private void AddChat(Avalonia.Media.IImage avatar, string nick, string msg, string time, int unread = 0)
        {
            var item = new ChatItem { Avatar = avatar, Nickname = nick, LastMessage = msg, Time = time, Unread = unread };
            item.PointerPressed += (_, _) => ChatSelected?.Invoke(item);
            ChatList.Children.Add(item);
        }

        // 搜索框 × : 清空并重新聚焦(× 由绑定在有文字时显示)
        private void ClearSearch_Click(object? sender, RoutedEventArgs e)
        {
            SearchBox.Text = string.Empty;
            SearchBox.Focus();
        }
    }
}
