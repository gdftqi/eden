using Avalonia;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Media;
using System;

namespace CC
{
    // 聊天窗口: 顶栏(对方信息) + 消息区(气泡挂这里) + 底部输入栏
    public partial class ChatWindow : UserControl
    {
        public static readonly StyledProperty<string?> PeerNameProperty =
            AvaloniaProperty.Register<ChatWindow, string?>(nameof(PeerName));

        public static readonly StyledProperty<IImage?> PeerAvatarProperty =
            AvaloniaProperty.Register<ChatWindow, IImage?>(nameof(PeerAvatar));

        // 对方最后在线状态(顶栏昵称下方小灰字)
        public static readonly StyledProperty<string?> PeerStatusProperty =
            AvaloniaProperty.Register<ChatWindow, string?>(nameof(PeerStatus));

        public string? PeerName { get => GetValue(PeerNameProperty); set => SetValue(PeerNameProperty, value); }
        public IImage? PeerAvatar { get => GetValue(PeerAvatarProperty); set => SetValue(PeerAvatarProperty, value); }
        public string? PeerStatus { get => GetValue(PeerStatusProperty); set => SetValue(PeerStatusProperty, value); }

        public ChatWindow()
        {
            InitializeComponent();
        }

        // 往消息区挂一个气泡(气泡都挂在聊天窗口里)
        public void AddMessage(MessageBubble bubble) => MessageList.Children.Add(bubble);

        // 便捷: 加一条文本消息
        public void AddText(bool outgoing, string text, string time, int status = 0)
            => AddMessage(new MessageBubble { IsOutgoing = outgoing, Text = text, Time = time, Status = status });

        public void ClearMessages() => MessageList.Children.Clear();

        // 发送(占位): 把输入框文字作为"自己发的"加进去; 真正的网络发送以后接
        private void Send_Click(object? sender, RoutedEventArgs e)
        {
            var t = InputBox.Text?.Trim();
            if (string.IsNullOrEmpty(t)) return;
            AddText(true, t, DateTime.Now.ToString("HH:mm"), 1);
            InputBox.Text = string.Empty;
            InputBox.Focus();
        }
    }
}
