using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;

namespace CC
{
    // 消息气泡: 外框(方向/颜色/时间/回执 —— 所有类型通用) + 内容(Text 走文本; Body 留给文件等其它类型)
    public partial class MessageBubble : UserControl
    {
        // 方向: true=自己发(右·绿), false=对方(左·白)
        public static readonly StyledProperty<bool> IsOutgoingProperty =
            AvaloniaProperty.Register<MessageBubble, bool>(nameof(IsOutgoing));

        public static readonly StyledProperty<string?> TimeProperty =
            AvaloniaProperty.Register<MessageBubble, string?>(nameof(Time));

        // 文本内容(常见类型; emoji 也走这里, 就是文本里的字符)
        public static readonly StyledProperty<string?> TextProperty =
            AvaloniaProperty.Register<MessageBubble, string?>(nameof(Text));

        // 非文本内容(文件/图片/语音…的内容控件); 有值时优先于 Text
        public static readonly StyledProperty<object?> BodyProperty =
            AvaloniaProperty.Register<MessageBubble, object?>(nameof(Body));

        // 回执(仅自己发的显示): 0 无 / 1 已发(单勾) / 2 已送达(双勾) / 3 已读(蓝双勾)
        public static readonly StyledProperty<int> StatusProperty =
            AvaloniaProperty.Register<MessageBubble, int>(nameof(Status));

        public bool IsOutgoing { get => GetValue(IsOutgoingProperty); set => SetValue(IsOutgoingProperty, value); }
        public string? Time { get => GetValue(TimeProperty); set => SetValue(TimeProperty, value); }
        public string? Text { get => GetValue(TextProperty); set => SetValue(TextProperty, value); }
        public object? Body { get => GetValue(BodyProperty); set => SetValue(BodyProperty, value); }
        public int Status { get => GetValue(StatusProperty); set => SetValue(StatusProperty, value); }

        static readonly IBrush InBrush = new SolidColorBrush(Color.Parse("#FFFFFF"));   // 对方: 白
        static readonly IBrush OutBrush = new SolidColorBrush(Color.Parse("#DCF8C6"));  // 自己: 浅绿
        static readonly IBrush TickGray = new SolidColorBrush(Color.Parse("#8696A0"));
        static readonly IBrush TickBlue = new SolidColorBrush(Color.Parse("#34B7F1"));

        const string SingleCheck = "M0,3 L2,5 L6,0";
        const string DoubleCheck = "M0,3 L2,5 L6,0 M3.5,3 L5.5,5 L9.5,0";

        public MessageBubble()
        {
            InitializeComponent();
            Apply();
        }

        protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
        {
            base.OnPropertyChanged(change);
            if (change.Property == IsOutgoingProperty || change.Property == StatusProperty)
                Apply();
        }

        // 方向/颜色/回执 —— 用代码控制, 避开 Avalonia 选择器对 bool 的繁琐处理
        private void Apply()
        {
            if (Bubble == null) return;

            HorizontalAlignment = IsOutgoing ? HorizontalAlignment.Right : HorizontalAlignment.Left;
            Bubble.Background = IsOutgoing ? OutBrush : InBrush;

            Ticks.IsVisible = IsOutgoing && Status > 0;
            if (Ticks.IsVisible)
            {
                Ticks.Stroke = Status >= 3 ? TickBlue : TickGray;
                Ticks.Data = Geometry.Parse(Status >= 2 ? DoubleCheck : SingleCheck);
            }
        }
    }
}
