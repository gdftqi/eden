using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Media;
using CC.Utils;
using System;
using System.Runtime.InteropServices;

namespace CC
{
    public partial class ChatWindow : UserControl
    {// 聊天窗口
        public static readonly StyledProperty<string?> PeerNameProperty = AvaloniaProperty.Register<ChatWindow, string?>(nameof(PeerName));
        public static readonly StyledProperty<IImage?> PeerAvatarProperty = AvaloniaProperty.Register<ChatWindow, IImage?>(nameof(PeerAvatar));
        public static readonly StyledProperty<string?> PeerStatusProperty = AvaloniaProperty.Register<ChatWindow, string?>(nameof(PeerStatus));

        public string? PeerName { get => GetValue(PeerNameProperty); set => SetValue(PeerNameProperty, value); }
        public IImage? PeerAvatar { get => GetValue(PeerAvatarProperty); set => SetValue(PeerAvatarProperty, value); }
        public string? PeerStatus { get => GetValue(PeerStatusProperty); set => SetValue(PeerStatusProperty, value); }

        public ChatWindow()
        {
            InitializeComponent();
            InputBox.AddHandler(InputElement.KeyDownEvent, InputBox_KeyDown, RoutingStrategies.Tunnel);
            BuildEmojiPicker();
        }

        private void AddMessage(MessageBubble bubble)
        {// 添加消息
            MessageList.Children.Add(bubble);
            ScrollToBottom();
        }

        private void ScrollToBottom()
        {// 滚动条到底
            EventHandler? onLayout = null;
            onLayout = (_, _) =>
            {
                MsgScroll.LayoutUpdated -= onLayout;
                MsgScroll.Offset = new Vector(0, MsgScroll.Extent.Height);
            };
            MsgScroll.LayoutUpdated += onLayout;
        }

        public void AddText(bool outgoing, string text, string time, int status = 0)
        {// 加一条文本消息
            AddMessage(new MessageBubble { IsOutgoing = outgoing, Text = text, Time = time, Status = status });
        }

        public void ClearMessages()
        {
            MessageList.Children.Clear();
        }

        // 发送消息请求
        public event Action<string>? SendRequested;

        private void Send_Click(object? sender, RoutedEventArgs e)
        {// 发送句柄
            Send();
        }

        private void InputBox_KeyDown(object? sender, KeyEventArgs e)
        {// 回车: 发送,  Alt+回车: 换行
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

        private void Send()
        {// 发送
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
        {// 创建 EmojiPicker
            emojiFlyout = (Flyout)EmojiBtn.Flyout!;
            var picker = new EmojiPicker();
            picker.EmojiSelected += InsertEmoji;
            emojiFlyout.Content = picker;
        }

        private void InsertEmoji(string emo)
        {// 选中表情
            int caret = InputBox.CaretIndex;
            var text = InputBox.Text ?? string.Empty;
            InputBox.Text = text.Substring(0, caret) + emo + text.Substring(caret);
            InputBox.CaretIndex = caret + emo.Length;
            emojiFlyout?.Hide();
            InputBox.Focus();
        }

        private void AddContact_Click(object? sender, RoutedEventArgs e)
        {// 添加联系人
            // TODO: 加为联系人
        }

        private void ClearChat_Click(object? sender, RoutedEventArgs e)
        {// 清空聊天
            ClearMessages();
        }

        private void DeleteChat_Click(object? sender, RoutedEventArgs e)
        {// 删除会话
            // TODO: 删除对话(需通知 MainWindow 移除会话并回到空态)
        }

        private void CaptureDesktop_Click(object? sender, RoutedEventArgs e)
        {// 桌面载图
            try
            {
                var topLevel = TopLevel.GetTopLevel(this);
                if (topLevel == null || topLevel.Screens == null || topLevel.Screens.Primary == null)
                {
                    return;
                }

                int screenWidth = topLevel.Screens.Primary.Bounds.Width;
                int screenHeight = topLevel.Screens.Primary.Bounds.Height;

                // 2. 调用封装好的核心截屏方法（这里以 Windows 为例）
                if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
                {
                    Sys.CaptureDesktop(screenWidth, screenHeight);
                }
                else
                {
                    
                }
            }
            catch (Exception ex)
            {
            }
        }
    }
}
