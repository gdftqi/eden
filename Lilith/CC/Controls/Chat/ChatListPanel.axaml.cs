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

        // 当前选中的会话项(高亮由 IsSelected 驱动, 不依赖焦点)
        private ChatItem? selectedItem;

        public ChatListPanel()
        {
            InitializeComponent();
        }

        private void AddChat(Avalonia.Media.IImage avatar, string nick, string msg, string time, int unread = 0)
        {// // 建一个会话项
            var item = new ChatItem { Avatar = avatar, Nickname = nick, LastMessage = msg, Time = time, Unread = unread };
            item.PointerPressed += (_, _) => Select(item);
            ChatList.Children.Add(item);
        }

        // 切换选中项: 旧的熄灭, 新的点亮, 再通知宿主
        private void Select(ChatItem item)
        {
            if (selectedItem != null)
            {
                selectedItem.IsSelected = false;
            }
            selectedItem = item;
            item.IsSelected = true;
            ChatSelected?.Invoke(item);
        }

        private void ClearSearch_Click(object? sender, RoutedEventArgs e)
        {// 搜索框 清空文字
            SearchBox.Text = string.Empty;
            SearchBox.Focus();
        }
    }
}
