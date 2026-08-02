using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Media;
using System;

namespace CC
{
    // 群聊窗口(部门也走这个).
    // 与 SingleChatWindow 是一对孪生控件: 消息区/输入框/表情/发送逻辑两边相同,
    // 改其中一处务必同步另一处.差异只在顶栏按钮和菜单项.
    public partial class MultiChatWindow : UserControl
    {
        public static readonly StyledProperty<string?> PeerNameProperty = AvaloniaProperty.Register<MultiChatWindow, string?>(nameof(PeerName));
        public static readonly StyledProperty<IImage?> PeerAvatarProperty = AvaloniaProperty.Register<MultiChatWindow, IImage?>(nameof(PeerAvatar));
        public static readonly StyledProperty<string?> PeerStatusProperty = AvaloniaProperty.Register<MultiChatWindow, string?>(nameof(PeerStatus));

        public string? PeerName { get => GetValue(PeerNameProperty); set => SetValue(PeerNameProperty, value); }
        public IImage? PeerAvatar { get => GetValue(PeerAvatarProperty); set => SetValue(PeerAvatarProperty, value); }
        public string? PeerStatus { get => GetValue(PeerStatusProperty); set => SetValue(PeerStatusProperty, value); }

        /// <summary>
        /// 点了顶栏: 请求打开群详情.宿主不订阅就等于没这功能.
        /// </summary>
        public event Action? DetailRequested;

        /// <summary>
        /// 点了顶栏的搜索: 请求搜索本会话的消息.
        /// </summary>
        public event Action? SearchRequested;

        /// <summary>
        /// 发送消息请求
        /// </summary>
        public event Action<string>? SendRequested;

        public MultiChatWindow()
        {
            InitializeComponent();
            InputBox.AddHandler(InputElement.KeyDownEvent, InputBox_KeyDown, RoutingStrategies.Tunnel);
            BuildEmojiPicker();
            ApplyPeerAvatar();
        }

        protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
        {
            base.OnPropertyChanged(change);
            if (change.Property == PeerAvatarProperty)
            {
                ApplyPeerAvatar();
            }
        }

        // 没头像就把圆圈整个收起来, 否则顶栏左边会留一块空白
        private void ApplyPeerAvatar()
        {
            if (PeerAvatarBox != null)
            {
                PeerAvatarBox.IsVisible = PeerAvatar != null;
            }
        }

        private void TopBar_PointerPressed(object? sender, PointerPressedEventArgs e)
        {
            // 按钮自己会把 PointerPressed 标记为已处理, 不会冒泡到这里,
            // 所以点右侧图标不会误触发详情
            DetailRequested?.Invoke();
        }

        // ---- 消息 ----

        private void AddMessage(MessageBubble bubble)
        {
            MessageList.Children.Add(bubble);
            ScrollToBottom();
        }

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

        public void AddText(bool outgoing, string text, string time, int status = 0)
        {
            AddMessage(new MessageBubble { IsOutgoing = outgoing, Text = text, Time = time, Status = status });
        }

        public void ClearMessages()
        {
            MessageList.Children.Clear();
        }

        // ---- 发送 ----

        private void Send_Click(object? sender, RoutedEventArgs e)
        {
            Send();
        }

        private void InputBox_KeyDown(object? sender, KeyEventArgs e)
        {// 回车: 发送,  Alt+回车: 换行
            if (e.Key != Key.Enter) return;

            if (e.KeyModifiers.HasFlag(KeyModifiers.Alt))
            {
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

        private void Send()
        {
            var t = InputBox.Text?.Trim();
            if (string.IsNullOrEmpty(t)) return;
            AddText(true, t, DateTime.Now.ToString("HH:mm"), 1);
            InputBox.Text = string.Empty;
            InputBox.Focus();
            SendRequested?.Invoke(t);
        }

        // ---- 表情选择 ----

        private Flyout? emojiFlyout;

        private void BuildEmojiPicker()
        {
            emojiFlyout = (Flyout)EmojiBtn.Flyout!;
            var picker = new EmojiPicker();
            picker.EmojiSelected += InsertEmoji;
            emojiFlyout.Content = picker;
        }

        private void InsertEmoji(string emo)
        {
            int caret = InputBox.CaretIndex;
            var text = InputBox.Text ?? string.Empty;
            InputBox.Text = text.Substring(0, caret) + emo + text.Substring(caret);
            InputBox.CaretIndex = caret + emo.Length;
            emojiFlyout?.Hide();
            InputBox.Focus();
        }

        // ---- 顶栏动作 ----

        private void Search_Click(object? sender, RoutedEventArgs e)
        {
            SearchRequested?.Invoke();
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
