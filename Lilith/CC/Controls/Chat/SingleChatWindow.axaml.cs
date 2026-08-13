using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Media;
using CC.Model;
using CC.Proto;
using CC.Utils;
using System;
using System.Runtime.InteropServices;

namespace CC
{
    // 单聊窗口.
    public partial class SingleChatWindow : UserControl
    {
        public static readonly StyledProperty<string?> PeerNameProperty = AvaloniaProperty.Register<SingleChatWindow, string?>(nameof(PeerName));
        public static readonly StyledProperty<IImage?> PeerAvatarProperty = AvaloniaProperty.Register<SingleChatWindow, IImage?>(nameof(PeerAvatar));
        public static readonly StyledProperty<string?> PeerStatusProperty = AvaloniaProperty.Register<SingleChatWindow, string?>(nameof(PeerStatus));

        public string? PeerName { get => GetValue(PeerNameProperty); set => SetValue(PeerNameProperty, value); }
        public IImage? PeerAvatar { get => GetValue(PeerAvatarProperty); set => SetValue(PeerAvatarProperty, value); }
        public string? PeerStatus { get => GetValue(PeerStatusProperty); set => SetValue(PeerStatusProperty, value); }

        /// <summary>
        /// 当前显示的会话
        /// </summary>
        public Model.ChatConversation? Conversation { get; set; }

        /// <summary>
        /// 点了顶栏: 请求打开联系人详情.宿主不订阅就等于没这功能.
        /// </summary>
        public event Action? DetailRequested;

        /// <summary>
        /// 点了顶栏的搜索: 请求搜索本会话的消息.
        /// </summary>
        public event Action? SearchRequested;

        /// <summary>
        /// 发送消息请求
        /// </summary>
        public event Action<SingleChatReq>? SendRequested;

        public SingleChatWindow()
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


        /// <summary>
        /// 追加一条消息气泡.
        /// </summary>
        public void AddMessage(ChatMessage msg)
        {
            AddMessage(new MessageBubble
            {
                Message    = msg,
                IsOutgoing = msg.Mine,
                Text       = msg.Content,
                Time       = DateTimeOffset.FromUnixTimeMilliseconds(msg.CreatedAt).LocalDateTime.ToString("HH:mm"),
                Status     = msg.Status,
            });
        }


        /// <summary>
        /// 把某条消息的气泡刷成它当前的状态(发送中转圈 -> 已送达绿勾).
        /// 找不到就当它不在当前视图里, 静默跳过.
        /// </summary>
        public void UpdateStatus(ChatMessage msg)
        {
            foreach (var child in MessageList.Children)
            {
                if (child is MessageBubble b && ReferenceEquals(b.Message, msg))
                {
                    b.Status = msg.Status;
                    b.Time   = DateTimeOffset.FromUnixTimeMilliseconds(msg.CreatedAt).LocalDateTime.ToString("HH:mm");
                    return;
                }
            }
        }


        /// <summary>
        /// 按 Conversation.Messages 重画整个消息区, 切换会话时调用.
        /// </summary>
        public void Reload()
        {
            ClearMessages();

            if (Conversation == null)
            {
                return;
            }

            foreach (var m in Conversation.Messages)
            {
                AddMessage(m);
            }
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
            if (Conversation == null)
            {
                return;
            }

            var t = InputBox.Text?.Trim();
            if (string.IsNullOrEmpty(t))
            {
                return;
            }

            SingleChatReq req = new SingleChatReq();
            req.Content = t;
            req.Type = MessageType.MtText;
            req.ToId = (uint)Conversation.PeerId;

            InputBox.Text = string.Empty;
            InputBox.Focus();

            SendRequested?.Invoke(req);
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

        private void CaptureDesktop_Click(object? sender, RoutedEventArgs e)
        {// 桌面截图
            try
            {
                var topLevel = TopLevel.GetTopLevel(this);
                if (topLevel == null || topLevel.Screens == null || topLevel.Screens.Primary == null)
                {
                    return;
                }

                int screenWidth = topLevel.Screens.Primary.Bounds.Width;
                int screenHeight = topLevel.Screens.Primary.Bounds.Height;

                if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
                {
                    Sys.CaptureDesktop(screenWidth, screenHeight);
                }
            }
            catch (Exception)
            {
            }
        }
    }
}
