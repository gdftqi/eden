using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Documents;
using Avalonia.Layout;
using Avalonia.Media;
using System.Collections.Generic;
using System.Text;

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
            ApplyText();
        }

        protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
        {
            base.OnPropertyChanged(change);
            if (change.Property == IsOutgoingProperty || change.Property == StatusProperty)
                Apply();
            else if (change.Property == TextProperty)
                ApplyText();
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

        // 把文本拆成 文字段/表情段, 各自字号拼进 Inlines:
        // 纯表情按数量放大; 混在文字里的表情统一 24(和选择列表一样大), 文字保持 14。
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
            // SelectableTextBlock 行高按控件字号(14)算, 不会因大 Run 长高 → 大表情会顶出气泡。
            // 含表情时按表情字号撑高行盒, 气泡随之长高容下表情。
            TextBlk.LineHeight = hasEmoji ? emojiSize * 1.3 : double.NaN;
        }

        // 按"是否表情"把字符串切成连续段
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

        // 表情段: 表情本体 + 变体选择符/ZWJ/肤色修饰(让它们跟表情归到一段)
        private static bool IsEmojiRune(int cp) =>
            IsEmojiLike(cp) || cp == 0xFE0F || cp == 0x200D || (cp >= 0x1F3FB && cp <= 0x1F3FF);

        // 返回纯表情消息里的表情数; 0 = 含普通文字(不放大)
        private static int EmojiCount(string? s)
        {
            if (string.IsNullOrWhiteSpace(s)) return 0;
            int count = 0;
            foreach (var r in s.EnumerateRunes())
            {
                int cp = r.Value;
                // 变体选择符 / ZWJ / 肤色修饰: 组合用, 跳过不计
                if (cp == 0xFE0F || cp == 0x200D || (cp >= 0x1F3FB && cp <= 0x1F3FF)) continue;
                if (Rune.IsWhiteSpace(r)) continue;
                if (!IsEmojiLike(cp)) return 0;   // 出现普通文字 → 不是纯表情
                count++;
            }
            return count <= 8 ? count : 0;        // 太多就不放大
        }

        private static bool IsEmojiLike(int cp) =>
            cp >= 0x1F000
            || (cp >= 0x2190 && cp <= 0x2BFF)     // 箭头/符号/装饰区(含 ⭐❤✨✌ 等)
            || cp == 0x2122 || cp == 0x2139 || cp == 0x3030 || cp == 0x303D;
    }
}
