using Avalonia.Controls;
using System;

namespace CC
{
    public partial class ChatTab : UserControl
    {
        // ChatWindow 里点发送时向外抛文字(网络收发仍在 MainWindow, 这里只管界面)
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
            ChatView.PeerStatus = "最后在线 今天 17:20";

            // 临时: 每个会话都用同一组示例消息(以后换成各自的真实消息)
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
