using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Media.Transformation;
using Avalonia.Threading;
using CC.Model;
using System;
using System.Collections.Generic;
using System.Linq;

namespace CC
{
    public partial class ContactPickerDrawer : UserControl
    {
        /// <summary>
        /// 勾选状态变化: (昵称, 现在是否选中).宿主据此增删自己的名单.
        /// </summary>
        public event Action<string, bool>? Toggled;

        // 抽屉宽度, 必须与 XAML 里 UserControl 的 Width 一致(关闭时要正好平移出右边界)
        private const double DRAWER_W = 360;

        // 与 XAML 里 TransformOperationsTransition 的 Duration 对齐, 略留一点余量
        private static readonly TimeSpan DRAWER_MS = TimeSpan.FromMilliseconds(260);

        // 当前勾上的人.只作显示用, 不是数据源
        private readonly HashSet<string> selected = new();

        // 昵称 -> 该行的复选框, 重建列表后按 selected 补勾
        private readonly Dictionary<string, CheckBox> checkBoxes = new();

        // 字母 -> 该组的标题控件, 点索引条时靠它滚过去
        private readonly Dictionary<char, Control> groupHeaders = new();

        public ContactPickerDrawer()
        {
            InitializeComponent();
            BuildPicker();
        }


        /// <summary>
        /// 抽屉标题.默认"选择联系人".
        /// </summary>
        public string Caption
        {
            get => TitleText.Text ?? string.Empty;
            set => TitleText.Text = value;
        }


        // ---------------- 开合 ----------------

        public bool IsOpen => IsVisible;


        public void Open()
        {
            IsVisible = true;

            // 先让它以"在右边界之外"的状态完成一次布局, 下一帧再平移到位.
            // 同一帧里既显示又改变换的话, 过渡没有起点, 会直接跳出来而不是滑出来.
            Dispatcher.UIThread.Post(
                () => RenderTransform = TransformOperations.Parse("translateX(0px)"),
                DispatcherPriority.Render);

            PickerSearch.Focus();
        }


        public void Close()
        {
            RenderTransform = TransformOperations.Parse($"translateX({DRAWER_W}px)");

            // 等滑出动画播完再隐藏 -- 立刻置 IsVisible=false 的话是"瞬间消失", 看不到动画
            DispatcherTimer.RunOnce(() => IsVisible = false, DRAWER_MS);
        }


        public void Toggle()
        {
            if (IsOpen)
            {
                Close();
            }
            else
            {
                Open();
            }
        }


        /// <summary>
        /// 不带动画地收起, 并清掉上次的搜索词.
        /// 用于宿主重置表单: 页面刚打开时抽屉本来就该是关的, 让它当着用户的面滑一次很怪.
        /// </summary>
        public void HideNow()
        {
            RenderTransform = TransformOperations.Parse($"translateX({DRAWER_W}px)");
            IsVisible = false;

            // 上次留下的过滤结果会让人以为联系人少了
            PickerSearch.Text = string.Empty;
            BuildPicker();
        }


        // ---------------- 勾选状态 ----------------

        /// <summary>
        /// 用宿主的名单刷新勾选状态.宿主那边名单一变就调一次
        /// (包括在抽屉外面移除成员的情况, 否则抽屉里的勾会和列表对不上).
        /// </summary>
        public void SetChecked(IEnumerable<string> nicknames)
        {
            selected.Clear();
            foreach (var n in nicknames)
            {
                selected.Add(n);
            }

            ApplyChecks();
        }


        private void ApplyChecks()
        {
            foreach (var kv in checkBoxes)
            {
                kv.Value.IsChecked = selected.Contains(kv.Key);
            }
        }


        private void ToggleOne(string nickname)
        {
            bool on = !selected.Remove(nickname);
            if (on)
            {
                selected.Add(nickname);
            }

            if (checkBoxes.TryGetValue(nickname, out var cb))
            {
                cb.IsChecked = on;
            }

            Toggled?.Invoke(nickname, on);
        }


        // ---------------- 列表 ----------------

        /// <summary>
        /// 按 Sort(拼音)排序后分组重建列表.keyword 非空时改为平铺的过滤结果,
        /// 此时索引条没有意义, 一并隐藏.
        /// </summary>
        private void BuildPicker(string keyword = "")
        {
            PickerList.Children.Clear();
            IndexBar.Children.Clear();
            groupHeaders.Clear();
            checkBoxes.Clear();

            var all = ContactSource.All
                .OrderBy(c => c.IndexKey == '#' ? 1 : 0)          // # 排最后
                .ThenBy(c => c.Sort, StringComparer.Ordinal)
                .ToList();

            if (keyword.Length > 0)
            {
                // 昵称和拼音都能命中 -- 打 "meinv" 或 "美女" 都该搜到
                var hits = all.Where(c =>
                    c.Nickname.Contains(keyword, StringComparison.OrdinalIgnoreCase) ||
                    c.Sort.Contains(keyword, StringComparison.OrdinalIgnoreCase)).ToList();

                IndexBar.IsVisible = false;

                if (hits.Count == 0)
                {
                    PickerList.Children.Add(new TextBlock
                    {
                        Text = "没有匹配的联系人",
                        FontSize = 13,
                        Foreground = new SolidColorBrush(Color.Parse("#9AA0A6")),
                        HorizontalAlignment = HorizontalAlignment.Center,
                        Margin = new Thickness(0, 22, 0, 22),
                    });
                    return;
                }

                foreach (var c in hits)
                {
                    PickerList.Children.Add(BuildPickerRow(c));
                }

                ApplyChecks();
                return;
            }

            IndexBar.IsVisible = true;

            char cur = '\0';
            foreach (var c in all)
            {
                if (c.IndexKey != cur)
                {
                    cur = c.IndexKey;
                    var header = BuildGroupHeader(cur);
                    groupHeaders[cur] = header;
                    PickerList.Children.Add(header);
                    IndexBar.Children.Add(BuildIndexEntry(cur));
                }

                PickerList.Children.Add(BuildPickerRow(c));
            }

            // 重建会丢掉勾选框的视觉状态, 就地补回来 --
            // 放在这里而不是交给调用方, 少一个"忘了补勾"的坑
            ApplyChecks();
        }


        private Control BuildGroupHeader(char key)
        {
            return new Border
            {
                Height = 24,
                Padding = new Thickness(10, 0),
                Background = new SolidColorBrush(Color.Parse("#F2F3F5")),
                Child = new TextBlock
                {
                    Text = key.ToString(),
                    FontSize = 11.5,
                    FontWeight = FontWeight.SemiBold,
                    Foreground = new SolidColorBrush(Color.Parse("#8A9099")),
                    VerticalAlignment = VerticalAlignment.Center,
                },
            };
        }


        // 索引条上的一个字母: 点它把对应分组滚进视野
        private Control BuildIndexEntry(char key)
        {
            var t = new TextBlock
            {
                Text = key.ToString(),
                FontSize = 10.5,
                Foreground = new SolidColorBrush(Color.Parse("#8A9099")),
                HorizontalAlignment = HorizontalAlignment.Center,
                Margin = new Thickness(0, 1, 0, 1),
            };

            var box = new Border
            {
                Background = Brushes.Transparent,
                CornerRadius = new CornerRadius(7),
                Padding = new Thickness(0, 1),
                Cursor = new Cursor(StandardCursorType.Hand),
                Child = t,
            };

            box.PointerPressed += (_, _) =>
            {
                if (groupHeaders.TryGetValue(key, out var header))
                {
                    header.BringIntoView();
                }
            };
            return box;
        }


        private Control BuildPickerRow(ContactInfo c)
        {
            var check = new CheckBox
            {
                IsChecked = false,
                // 只作显示: 点击由整行的 PointerPressed 统一处理, 免得点方框和点空白行为不一致
                IsHitTestVisible = false,
                VerticalAlignment = VerticalAlignment.Center,
                Margin = new Thickness(0, 0, 6, 0),
            };
            checkBoxes[c.Nickname] = check;

            var avatar = new Border
            {
                Width = 30,
                Height = 30,
                CornerRadius = new CornerRadius(15),
                ClipToBounds = true,
                Margin = new Thickness(0, 0, 10, 0),
                Child = new Image { Source = Avatars.Default, Stretch = Stretch.UniformToFill },
            };

            var name = new TextBlock
            {
                Text = c.Nickname,
                FontSize = 13,
                Foreground = new SolidColorBrush(Color.Parse("#1E1E1E")),
                VerticalAlignment = VerticalAlignment.Center,
                TextTrimming = TextTrimming.CharacterEllipsis,
            };

            var row = new StackPanel { Orientation = Orientation.Horizontal };
            row.Children.Add(check);
            row.Children.Add(avatar);
            row.Children.Add(name);

            var body = new Border
            {
                Height = 44,
                Padding = new Thickness(10, 0),
                Background = Brushes.Transparent,
                Cursor = new Cursor(StandardCursorType.Hand),
                Child = row,
            };
            body.PointerPressed += (_, _) => ToggleOne(c.Nickname);
            return body;
        }


        private void PickerSearch_TextChanged(object? sender, TextChangedEventArgs e)
        {
            BuildPicker((PickerSearch.Text ?? string.Empty).Trim());
        }


        private void Close_Click(object? sender, RoutedEventArgs e)
        {
            Close();
        }
    }
}
