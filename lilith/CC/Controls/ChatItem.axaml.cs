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

        public ChatItem()
        {
            InitializeComponent();
        }
    }
}
