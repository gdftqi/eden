using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;

namespace CC
{
    // 会话列表项: 头像 + 昵称 + 最后一条消息 + 时间
    public partial class ChatItem : UserControl
    {
        public static readonly StyledProperty<string?> NicknameProperty =
            AvaloniaProperty.Register<ChatItem, string?>(nameof(Nickname));

        public static readonly StyledProperty<string?> LastMessageProperty =
            AvaloniaProperty.Register<ChatItem, string?>(nameof(LastMessage));

        public static readonly StyledProperty<string?> TimeProperty =
            AvaloniaProperty.Register<ChatItem, string?>(nameof(Time));

        public static readonly StyledProperty<IImage?> AvatarProperty =
            AvaloniaProperty.Register<ChatItem, IImage?>(nameof(Avatar));

        // 未读数: 0 不显示角标; >99 显示 "99+"
        public static readonly StyledProperty<int> UnreadProperty =
            AvaloniaProperty.Register<ChatItem, int>(nameof(Unread));

        public string? Nickname
        {
            get => GetValue(NicknameProperty);
            set => SetValue(NicknameProperty, value);
        }

        public string? LastMessage
        {
            get => GetValue(LastMessageProperty);
            set => SetValue(LastMessageProperty, value);
        }

        public string? Time
        {
            get => GetValue(TimeProperty);
            set => SetValue(TimeProperty, value);
        }

        public IImage? Avatar
        {
            get => GetValue(AvatarProperty);
            set => SetValue(AvatarProperty, value);
        }

        public int Unread
        {
            get => GetValue(UnreadProperty);
            set => SetValue(UnreadProperty, value);
        }

        public ChatItem()
        {
            InitializeComponent();
            ApplyUnread();
        }

        protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
        {
            base.OnPropertyChanged(change);
            if (change.Property == UnreadProperty)
                ApplyUnread();
        }

        // 未读角标: 0 隐藏; 1..99 显示数字; >99 显示 "99+"
        private void ApplyUnread()
        {
            if (Badge == null) return;
            Badge.IsVisible = Unread > 0;
            BadgeText.Text = Unread > 99 ? "99+" : Unread.ToString();
        }
    }
}
