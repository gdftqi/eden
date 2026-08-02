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
    // 创建部门表单: 部门名称 + 分割线 + 添加人员(从联系人里勾选).
    // 版式与 OrgDetailPanel 保持一致, 成员行的构建方式也刻意与那边相同 --
    // 两个面板前后脚出现在同一个位置, 长得不一样会很跳.
    // 与 OrgListPanel 同样的分工: 本控件只管收集与校验, 存到哪去由宿主(OrgTab)决定.
    public partial class OrgEditPanel : UserControl
    {
        // 点"确定"且校验通过时触发: (部门名称, 成员昵称列表)
        public event Action<string, IReadOnlyList<string>>? Saved;

        // 点"取消"时触发
        public event Action? Cancelled;

        // 已勾选的成员(按昵称去重).以后接服务端换成 uid.
        private readonly HashSet<string> selected = new();

        // 昵称 -> 该联系人在勾选列表里的那个复选框, 用于点整行时同步打勾
        private readonly Dictionary<string, CheckBox> checkBoxes = new();

        public OrgEditPanel()
        {
            InitializeComponent();
            BuildPicker();
            RefreshMembers();
        }


        /// <summary>
        /// 清空表单并把焦点放到部门名称.每次打开都调一次,
        /// 否则会看到上一次没提交的残留内容.
        /// </summary>
        public void Reset()
        {
            NameBox.Text = string.Empty;
            ErrorTip.IsVisible = false;
            HideDrawerNow();

            selected.Clear();

            // 连搜索词一起清掉并重建 -- 上次留下的过滤结果会让人以为联系人少了
            PickerSearch.Text = string.Empty;
            BuildPicker();

            RefreshMembers();
            NameBox.Focus();
        }


        // ---------------- 联系人勾选列表 ----------------

        // 字母 -> 该组的标题控件, 点索引条时靠它滚过去
        private readonly Dictionary<char, Control> groupHeaders = new();


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


        private void PickerSearch_TextChanged(object? sender, TextChangedEventArgs e)
        {
            BuildPicker((PickerSearch.Text ?? string.Empty).Trim());

            // 重建列表会丢掉勾选框的视觉状态, 按 selected 重新点亮
            foreach (var kv in checkBoxes)
            {
                kv.Value.IsChecked = selected.Contains(kv.Key);
            }
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
                Child = new Image { Source = ContactSource.Avatar, Stretch = Stretch.UniformToFill },
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
            body.PointerPressed += (_, _) => Toggle(c.Nickname);
            return body;
        }


        private void Toggle(string nickname)
        {
            if (!selected.Remove(nickname))
            {
                selected.Add(nickname);
            }

            if (checkBoxes.TryGetValue(nickname, out var cb))
            {
                cb.IsChecked = selected.Contains(nickname);
            }

            RefreshMembers();
        }


        // ---------------- 右侧选人抽屉 ----------------

        // 抽屉宽度, 必须与 XAML 里 PickerDrawer 的 Width 一致(关闭时要正好平移出右边界)
        private const double DRAWER_W = 360;

        // 与 XAML 里 TransformOperationsTransition 的 Duration 对齐, 略留一点余量
        private static readonly TimeSpan DRAWER_MS = TimeSpan.FromMilliseconds(260);


        private void TogglePicker_PointerPressed(object? sender, PointerPressedEventArgs e)
        {
            if (PickerDrawer.IsVisible)
            {
                CloseDrawer();
            }
            else
            {
                OpenDrawer();
            }
        }


        private void ClosePicker_Click(object? sender, RoutedEventArgs e)
        {
            CloseDrawer();
        }


        private void OpenDrawer()
        {
            PickerDrawer.IsVisible = true;

            // 先让它以"在右边界之外"的状态完成一次布局, 下一帧再平移到位.
            // 同一帧里既显示又改变换的话, 过渡没有起点, 会直接跳出来而不是滑出来.
            Dispatcher.UIThread.Post(
                () => PickerDrawer.RenderTransform = TransformOperations.Parse("translateX(0px)"),
                DispatcherPriority.Render);

            PickerSearch.Focus();
        }


        private void CloseDrawer()
        {
            PickerDrawer.RenderTransform = TransformOperations.Parse($"translateX({DRAWER_W}px)");

            // 等滑出动画播完再隐藏 -- 立刻置 IsVisible=false 的话是"瞬间消失", 看不到动画
            DispatcherTimer.RunOnce(() => PickerDrawer.IsVisible = false, DRAWER_MS);
        }


        // 不带动画地收起.用于 Reset(): 打开创建页时抽屉应当已经是关的,
        // 让它当着用户的面滑一次很怪.
        private void HideDrawerNow()
        {
            PickerDrawer.RenderTransform = TransformOperations.Parse($"translateX({DRAWER_W}px)");
            PickerDrawer.IsVisible = false;
        }


        // ---------------- 已选成员(行式, 与 OrgDetailPanel 一致) ----------------

        // 全量重建.成员通常只有几个, 比增量维护简单且不会错位.
        private void RefreshMembers()
        {
            MemberList.Children.Clear();
            foreach (var nick in selected)
            {
                MemberList.Children.Add(BuildMemberRow(nick));
            }

            MemberHeader.Text = selected.Count > 0 ? $"{selected.Count} 位成员" : "尚未选择成员";
        }


        private Control BuildMemberRow(string nickname)
        {
            // 签名从联系人表里查; 查不到就留空, 不编造内容
            var sign = ContactSource.All.FirstOrDefault(c => c.Nickname == nickname)?.Sign ?? string.Empty;

            var avatar = new Border
            {
                Width = 42,
                Height = 42,
                CornerRadius = new CornerRadius(21),
                ClipToBounds = true,
                Margin = new Thickness(0, 0, 14, 0),
                Child = new Image { Source = ContactSource.Avatar, Stretch = Stretch.UniformToFill },
            };

            var texts = new StackPanel { VerticalAlignment = VerticalAlignment.Center, Spacing = 2 };
            texts.Children.Add(new TextBlock
            {
                Text = nickname,
                FontSize = 14,
                Foreground = new SolidColorBrush(Color.Parse("#1E1E1E")),
                TextTrimming = TextTrimming.CharacterEllipsis,
            });

            if (sign.Length > 0)
            {
                texts.Children.Add(new TextBlock
                {
                    Text = sign,
                    FontSize = 12,
                    Foreground = new SolidColorBrush(Color.Parse("#8A9099")),
                    TextTrimming = TextTrimming.CharacterEllipsis,
                });
            }

            // 右侧一个"移除"的叉, 不用滚回勾选列表里找
            var remove = new Avalonia.Controls.Shapes.Path
            {
                Width = 12,
                Height = 12,
                Stroke = new SolidColorBrush(Color.Parse("#9AA0A6")),
                StrokeThickness = 1.6,
                StrokeLineCap = PenLineCap.Round,
                Data = Geometry.Parse("M1,1 L11,11 M11,1 L1,11"),
                VerticalAlignment = VerticalAlignment.Center,
            };

            var grid = new Grid { ColumnDefinitions = new ColumnDefinitions("Auto,*,Auto") };
            Grid.SetColumn(avatar, 0);
            Grid.SetColumn(texts, 1);
            Grid.SetColumn(remove, 2);
            grid.Children.Add(avatar);
            grid.Children.Add(texts);
            grid.Children.Add(remove);

            var row = new Border { Child = grid };
            row.Classes.Add("row");   // 复用 XAML 里 Border.row 的高度/内边距/hover
            ToolTip.SetTip(row, "点击移除");
            row.PointerPressed += (_, _) => Toggle(nickname);
            return row;
        }


        // ---------------- 提交 ----------------

        private void Save_Click(object? sender, RoutedEventArgs e)
        {
            var name = (NameBox.Text ?? string.Empty).Trim();
            if (name.Length == 0)
            {
                ShowError("请填写部门名称");
                return;
            }

            ErrorTip.IsVisible = false;
            Saved?.Invoke(name, selected.ToList());
        }


        private void Cancel_Click(object? sender, RoutedEventArgs e)
        {
            Cancelled?.Invoke();
        }


        /// <summary>
        /// 显示一条错误提示.公开出去是为了让宿主也能用 --
        /// 以后接服务端时, "部门已存在"这类只有服务端知道的错误要回显在同一个位置.
        /// </summary>
        public void ShowError(string msg)
        {
            ErrorTip.Text = msg;
            ErrorTip.IsVisible = true;
        }
    }
}
