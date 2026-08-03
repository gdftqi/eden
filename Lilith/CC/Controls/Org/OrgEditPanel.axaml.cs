using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Layout;
using Avalonia.Media;
using CC.Model;
using System;
using System.Collections.Generic;
using System.Linq;

namespace CC
{
    // 创建部门表单: 部门名称 + 分割线 + 添加人员(从右侧抽屉里勾选).
    // 版式与 OrgDetailPanel 保持一致, 成员行的构建方式也刻意与那边相同 --
    // 两个面板前后脚出现在同一个位置, 长得不一样会很跳.
    // 与 OrgListPanel 同样的分工: 本控件只管收集与校验, 存到哪去由宿主(OrgTab)决定.
    public partial class OrgEditPanel : UserControl
    {
        // 点"确定"且校验通过时触发: (部门名称, 成员昵称列表)
        public event Action<string, IReadOnlyList<string>>? Saved;

        // 点"取消"时触发
        public event Action? Cancelled;

        // 已勾选的成员(按昵称去重).这是待提交的草稿, 抽屉只是选人的界面.
        // 以后接服务端换成 uid.
        private readonly HashSet<string> selected = new();

        public OrgEditPanel()
        {
            InitializeComponent();
            Picker.Toggled += OnPickerToggled;
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

            selected.Clear();
            Picker.SetChecked(selected);
            Picker.HideNow();

            RefreshMembers();
            NameBox.Focus();
        }


        // ---------------- 选人抽屉 ----------------

        private void TogglePicker_PointerPressed(object? sender, PointerPressedEventArgs e)
        {
            Picker.Toggle();
        }


        // 抽屉里勾/取消了一个人
        private void OnPickerToggled(string nickname, bool on)
        {
            if (on)
            {
                selected.Add(nickname);
            }
            else
            {
                selected.Remove(nickname);
            }

            RefreshMembers();
        }


        // 在下面的已选列表里点叉移除.抽屉那边的勾要跟着灭, 否则两处对不上
        private void RemoveMember(string nickname)
        {
            selected.Remove(nickname);
            Picker.SetChecked(selected);
            RefreshMembers();
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
            row.PointerPressed += (_, _) => RemoveMember(nickname);
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
