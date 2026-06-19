using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
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
            // 回车=发送, Alt+回车=换行: 用 Tunnel 抢在 TextBox 默认换行处理之前拦截
            InputBox.AddHandler(InputElement.KeyDownEvent, InputBox_KeyDown, RoutingStrategies.Tunnel);
            BuildEmojiPicker();
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

        private void Send_Click(object? sender, RoutedEventArgs e) => Send();

        // 回车=发送 / Alt+回车=换行
        private void InputBox_KeyDown(object? sender, KeyEventArgs e)
        {
            if (e.Key != Key.Enter) return;

            if (e.KeyModifiers.HasFlag(KeyModifiers.Alt))
            {
                // Alt+回车: 在光标处插入换行
                int caret = InputBox.CaretIndex;
                var text = InputBox.Text ?? string.Empty;
                InputBox.Text = text.Substring(0, caret) + "\n" + text.Substring(caret);
                InputBox.CaretIndex = caret + 1;
            }
            else
            {
                Send();
            }
            e.Handled = true;
        }

        // 发送: 本地先显示"自己发的"气泡, 再把文字交给宿主走网络
        private void Send()
        {
            var t = InputBox.Text?.Trim();
            if (string.IsNullOrEmpty(t)) return;
            AddText(true, t, DateTime.Now.ToString("HH:mm"), 1);
            InputBox.Text = string.Empty;
            InputBox.Focus();
            SendRequested?.Invoke(t);
        }

        // ---- 表情选择(列表+网格在 EmojiPicker 组件里) ----
        private Flyout? _emojiFlyout;

        // 把 EmojiPicker 组件塞进表情按钮的 Flyout, 选中回调插到输入框
        private void BuildEmojiPicker()
        {
            _emojiFlyout = (Flyout)EmojiBtn.Flyout!;
            var picker = new EmojiPicker();
            picker.EmojiSelected += InsertEmoji;
            _emojiFlyout.Content = picker;
        }

        // 选中表情: 插到光标处, 关闭弹窗, 焦点回输入框
        private void InsertEmoji(string emo)
        {
            int caret = InputBox.CaretIndex;
            var text = InputBox.Text ?? string.Empty;
            InputBox.Text = text.Substring(0, caret) + emo + text.Substring(caret);
            InputBox.CaretIndex = caret + emo.Length;
            _emojiFlyout?.Hide();
            InputBox.Focus();
        }

        // ---- 顶栏"更多"下拉菜单 ----
        private void AddContact_Click(object? sender, RoutedEventArgs e)
        {
            // TODO: 加为联系人
        }

        private void ClearChat_Click(object? sender, RoutedEventArgs e)
        {
            ClearMessages();
        }

        private void DeleteChat_Click(object? sender, RoutedEventArgs e)
        {
            // TODO: 删除对话(需通知 MainWindow 移除会话并回到空态)
        }
    }
}
