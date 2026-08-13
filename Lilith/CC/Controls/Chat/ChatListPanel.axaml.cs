using Avalonia.Controls;
using Avalonia.Interactivity;
using System;
using System.Collections.Generic;

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
        /// 遍历列表里的所有会话.
        /// </summary>
        public IEnumerable<CC.Model.ChatConversation> Conversations
        {
            get
            {
                foreach (var child in ChatList.Children)
                {
                    if (child is ChatItem it && it.Conversation != null)
                    {
                        yield return it.Conversation;
                    }
                }
            }
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
        /// 会话进列表
        /// </summary>
        public ChatItem Upsert(CC.Model.ChatConversation conv, bool bump = false)
        {
            var item = Find(conv.ChatId);
            if (item == null)
            {
                item = new ChatItem { Conversation = conv };
                item.PointerPressed += (_, _) => Select(item);
                ChatList.Children.Insert(0, item);
            }
            else if (bump && ChatList.Children.IndexOf(item) > 0)
            {
                ChatList.Children.Remove(item);
                ChatList.Children.Insert(0, item);
            }

            item.Nickname    = conv.PeerName;
            item.LastMessage = conv.LastPreview;
            item.Time        = conv.LastTime == 0 ? string.Empty
                             : DateTimeOffset.FromUnixTimeMilliseconds(conv.LastTime).LocalDateTime.ToString("HH:mm");
            item.Unread      = conv.Unread;
            item.Avatar      = Avatars.Cached(conv.PeerAvatar) ?? Avatars.Default;

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


        /// <summary>
        /// 把某个会话从列表里摘掉(删除会话用).
        /// </summary>
        public void Remove(ChatItem item)
        {
            if (ReferenceEquals(selectedItem, item))
            {
                selectedItem = null;
            }

            ChatList.Children.Remove(item);
        }


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
        {
            SearchBox.Text = string.Empty;
            SearchBox.Focus();
        }
    }
}
