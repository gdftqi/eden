using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;
using System;

namespace CC
{

    public partial class EmojiPicker : UserControl
    {// 表情选择器
        // 选中某个表情时触发
        public event Action<string>? EmojiSelected;

        private static readonly string[] Emojis =
        {
            "😀","😃","😄","😁","😆","😅","😂","🤣","😊","😇","🙂","🙃","😉","😌","😍","🥰","😘","😗","😙","😚",
            "😋","😛","😜","🤪","😝","🤗","🤭","🤫","🤔","😐","😶","😏","😒","🙄","😬","😴","😪","😔","😕","🙁",
            "😣","😖","😫","😩","🥺","😢","😭","😤","😠","😡","🤬","😳","🥵","🥶","😱","😨","🤯","😬","😈","💀",
            "💩","🤡","👻","👽","🤖","😺","😻","🙀","👍","👎","👌","✌️","🤞","🤟","🤘","👏","🙏","💪","🤝","👀",
            "❤️","🧡","💛","💚","💙","💜","🖤","💔","💕","💞","💓","🔥","✨","⭐","🌟","💫","🎉","🎊","🌹","💯"
        };

        public EmojiPicker()
        {
            InitializeComponent();
            foreach (var emo in Emojis)
            {
                var btn = new Button
                {
                    Content = emo,
                    FontSize = 28,
                    Width = 40,
                    Height = 40,
                    Padding = new Thickness(0),
                    Background = Brushes.Transparent,
                    BorderThickness = new Thickness(0)
                };
                btn.Click += (_, _) => EmojiSelected?.Invoke(emo);
                Panel.Children.Add(btn);
            }
        }
    }
}
