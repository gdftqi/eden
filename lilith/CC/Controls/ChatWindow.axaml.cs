using Avalonia;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Media;
using Avalonia.Threading;
using System;

namespace CC
{
    // 聊天窗口: 顶栏(对方信息) + 消息区(气泡挂这里) + 底部输入栏
    public partial class ChatWindow : UserControl
    {
        public static readonly StyledProperty<string?> PeerNameProperty = AvaloniaProperty.Register<ChatWindow, string?>(nameof(PeerName));
        public static readonly StyledProperty<IImage?> PeerAvatarProperty = AvaloniaProperty.Register<ChatWindow, IImage?>(nameof(PeerAvatar));

        // 对方最后在线状态(顶栏昵称下方小灰字)
        public static readonly StyledProperty<string?> PeerStatusProperty = AvaloniaProperty.Register<ChatWindow, string?>(nameof(PeerStatus));

        public string? PeerName { get => GetValue(PeerNameProperty); set => SetValue(PeerNameProperty, value); }
        public IImage? PeerAvatar { get => GetValue(PeerAvatarProperty); set => SetValue(PeerAvatarProperty, value); }
        public string? PeerStatus { get => GetValue(PeerStatusProperty); set => SetValue(PeerStatusProperty, value); }

        public ChatWindow()
        {
            InitializeComponent();
        }

        // 往消息区挂一个气泡(气泡都挂在聊天窗口里), 挂完滚到底
        public void AddMessage(MessageBubble bubble)
        {
            MessageList.Children.Add(bubble);
            ScrollToBottom();
        }

        // 滚到底: 等这次布局真正结束(新气泡已测量, Extent 才是最终高度)再滚,
        // 否则用旧的 Extent 会差一个气泡的高度, 最后一条露半截。
        private void ScrollToBottom()
        {
            EventHandler? onLayout = null;
            onLayout = (_, _) =>
            {
                MsgScroll.LayoutUpdated -= onLayout;
                MsgScroll.Offset = new Vector(0, MsgScroll.Extent.Height);
            };
            MsgScroll.LayoutUpdated += onLayout;
        }

        // 便捷: 加一条文本消息
        public void AddText(bool outgoing, string text, string time, int status = 0)
            => AddMessage(new MessageBubble { IsOutgoing = outgoing, Text = text, Time = time, Status = status });

        public void ClearMessages() => MessageList.Children.Clear();

        // 发送时触发; 宿主(MainWindow)订阅后做真正的网络发送
        public event Action<string>? SendRequested;

        // 发送: 本地先显示"自己发的"气泡, 再把文字交给宿主走网络
        private void Send_Click(object? sender, RoutedEventArgs e)
        {
            var t = InputBox.Text?.Trim();
            if (string.IsNullOrEmpty(t)) return;
            AddText(true, t, DateTime.Now.ToString("HH:mm"), 1);
            InputBox.Text = string.Empty;
            InputBox.Focus();
            SendRequested?.Invoke(t);
        }
    }
}
