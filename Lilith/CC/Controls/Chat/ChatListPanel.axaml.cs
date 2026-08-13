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

        /// <summary>
        /// 按 chat_id 找会话行, 没有返回 null.
        /// </summary>
        public ChatItem? Find(Int64 chatId)
        {
            foreach (var child in ChatList.Children)
            {
                if (child is ChatItem it && it.Conversation?.ChatId == chatId)
                {
                    return it;
                }
            }

            return null;
        }


        /// <summary>
        /// 会话进列表: 已有则刷新展示, 没有则插到最顶(最近会话在上).
        /// </summary>
        public ChatItem Upsert(CC.Model.ChatConversation conv)
        {
            var item = Find(conv.ChatId);
            if (item == null)
            {
                item = new ChatItem { Conversation = conv };
                item.PointerPressed += (_, _) => Select(item);
                ChatList.Children.Insert(0, item);
            }

            item.Nickname    = conv.PeerName;
            item.LastMessage = conv.LastPreview;
            item.Time        = conv.LastTime == 0 ? string.Empty
                             : DateTimeOffset.FromUnixTimeMilliseconds(conv.LastTime).LocalDateTime.ToString("HH:mm");
            item.Unread      = conv.Unread;
            item.Avatar      = Avatars.Cached(conv.PeerAvatar) ?? Avatars.Default;

            // 头像不在缓存就异步补(item 只服务这一个会话, 不存在贴错)
            if (Avatars.Cached(conv.PeerAvatar) == null && !string.IsNullOrEmpty(conv.PeerAvatar))
            {
                _ = Avatars.Load(conv.PeerAvatar).ContinueWith(t =>
                {
                    if (t.Result != null)
                    {
                        item.Avatar = t.Result;
                    }
                }, System.Threading.Tasks.TaskScheduler.FromCurrentSynchronizationContext());
            }

            return item;
        }


        // 切换选中项: 旧的熄灭, 新的点亮, 再通知宿主
        public void Select(ChatItem item)
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
