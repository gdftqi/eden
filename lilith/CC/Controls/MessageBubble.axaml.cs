using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Documents;
using Avalonia.Layout;
using Avalonia.Media;
using System.Collections.Generic;
using System.Text;

namespace CC
{
    public partial class MessageBubble : UserControl
    {// 消息气泡
        // 方向: true=自己的消息, false=对方的消息
        public static readonly StyledProperty<bool> IsOutgoingProperty = AvaloniaProperty.Register<MessageBubble, bool>(nameof(IsOutgoing));
        public static readonly StyledProperty<string?> TimeProperty = AvaloniaProperty.Register<MessageBubble, string?>(nameof(Time));

        // 文本内容
        public static readonly StyledProperty<string?> TextProperty = AvaloniaProperty.Register<MessageBubble, string?>(nameof(Text));

        // 非文本内容
        public static readonly StyledProperty<object?> BodyProperty = AvaloniaProperty.Register<MessageBubble, object?>(nameof(Body));

        // 回执(仅自己发的显示): 0 无 / 1 已送达(单勾) / 2 已读(双勾)
        public static readonly StyledProperty<int> StatusProperty = AvaloniaProperty.Register<MessageBubble, int>(nameof(Status));

        public bool IsOutgoing {
            get => GetValue(IsOutgoingProperty); set => SetValue(IsOutgoingProperty, value); 
        }
        public string? Time {
            get => GetValue(TimeProperty); set => SetValue(TimeProperty, value);
        }
        public string? Text { get => GetValue(TextProperty); set => SetValue(TextProperty, value); 
        }
        public object? Body { get => GetValue(BodyProperty); set => SetValue(BodyProperty, value); 
        }
        public int Status { get => GetValue(StatusProperty); set => SetValue(StatusProperty, value);
        }

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
            ApplyText();
        }

        protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
        {
            base.OnPropertyChanged(change);
            if (change.Property == IsOutgoingProperty || change.Property == StatusProperty)
            {
                Apply();
            }
            else if (change.Property == TextProperty)
            {
                ApplyText();
            }
        }

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

        private void ApplyText()
        {
            if (TextBlk == null) return;
            var inlines = TextBlk.Inlines;
            if (inlines == null) return;
            inlines.Clear();

            var s = Text ?? "";
            if (s.Length == 0) return;

            double emojiSize = EmojiCount(s) switch
            {
                0      => 24,   // 含普通文字: 表情和选择列表一样大
                1      => 32,   // 纯表情 1 个: 最大
                2 or 3 => 28,
                _      => 24
            };

            bool hasEmoji = false;
            foreach (var (seg, isEmoji) in SplitRuns(s))
            {
                if (isEmoji) hasEmoji = true;
                inlines.Add(new Run(seg) { FontSize = isEmoji ? emojiSize : 14 });
            }

            TextBlk.LineHeight = hasEmoji ? emojiSize * 1.3 : double.NaN;
        }

        private static IEnumerable<(string seg, bool isEmoji)> SplitRuns(string s)
        {
            var sb = new StringBuilder();
            bool cur = false, started = false;
            foreach (var r in s.EnumerateRunes())
            {
                bool isEmoji = IsEmojiRune(r.Value);
                if (!started) { cur = isEmoji; started = true; }
                else if (isEmoji != cur)
                {
                    yield return (sb.ToString(), cur);
                    sb.Clear();
                    cur = isEmoji;
                }
                sb.Append(r.ToString());
            }
            if (sb.Length > 0) yield return (sb.ToString(), cur);
        }

        private static bool IsEmojiRune(int cp)
        {// 表情部分
            return IsEmojiLike(cp) || cp == 0xFE0F || cp == 0x200D || (cp >= 0x1F3FB && cp <= 0x1F3FF);
        }

        private static int EmojiCount(string? s)
        {
            if (string.IsNullOrWhiteSpace(s)) return 0;
            int count = 0;
            foreach (var r in s.EnumerateRunes())
            {
                int cp = r.Value;
                if (cp == 0xFE0F || cp == 0x200D || (cp >= 0x1F3FB && cp <= 0x1F3FF) || Rune.IsWhiteSpace(r))
                {
                    continue;
                }

                if (!IsEmojiLike(cp))
                {
                    return 0;
                }
                count++;
            }
            return count <= 8 ? count : 0;
        }

        private static bool IsEmojiLike(int cp)
        {
            return cp >= 0x1F000 || (cp >= 0x2190 && cp <= 0x2BFF) || cp == 0x2122 || cp == 0x2139 || cp == 0x3030 || cp == 0x303D;
        }
    }
}
