using Avalonia.Controls;
using System;

namespace CC
{
    public partial class ChatTab : BaseTab
    {
        public event Action<string>? SendRequested;

        public ChatTab()
        {
            InitializeComponent();
            ChatPanel.ChatSelected += OpenChat;
            ChatView.SendRequested += t => SendRequested?.Invoke(t);
        }

        private void OpenChat(ChatItem item)
        {
            ChatView.PeerName = item.Nickname;
            ChatView.PeerAvatar = item.Avatar;
            ChatView.PeerStatus = string.Empty;

            ChatView.ClearMessages();
            EmptyState.IsVisible = false;
            ChatView.IsVisible = true;
        }

        // 服务端来的消息进聊天窗(MainWindow.OnPackage 转发过来)
        public void AddText(bool mine, string text, string time)
        {
            ChatView.AddText(mine, text, time);
        }
    }
}
