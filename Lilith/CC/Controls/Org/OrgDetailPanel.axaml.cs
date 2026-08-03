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
    public partial class OrgDetailPanel : UserControl
    {
        public event Action? BackRequested;

        // 重命名部门 / 编辑部门描述 -- 都要落到服务端, 本控件不自己改
        public event Action<OrgItem>? RenameRequested;
        public event Action<OrgItem>? RemarkEditRequested;

        // 在选人抽屉里勾/取消了一个人: (部门, 昵称, 是否加入).
        // 同样不自己改 dept.Members -- 这是要发服务端的动作
        public event Action<OrgItem, string, bool>? MemberToggled;

        // 搜索本部门的消息
        public event Action<OrgItem>? SearchRequested;

        // 当前展示的部门
        private OrgItem? dept;

        public OrgDetailPanel()
        {
            InitializeComponent();
            Picker.Toggled += OnPickerToggled;
        }


        /// <summary>
        /// 用某个部门的数据填充.每次打开都调, 不缓存 --
        /// 部门信息随时可能被别处改掉, 缓存了就会显示过期内容.
        /// </summary>
        public void Show(OrgItem item)
        {
            dept = item;

            // 进页面时抽屉应当是关的: 上次离开时可能正开着
            Picker.HideNow();

            Refresh();
        }


        /// <summary>
        /// 按当前部门重画, 不动抽屉.宿主改完成员后调它 --
        /// 走 Show() 会把抽屉一起关掉, 就没法接着勾第二个人了.
        /// </summary>
        public void Refresh()
        {
            if (dept == null)
            {
                return;
            }

            DeptTitle.Text = dept.DeptName;
            MemberSummary.Text = $"{dept.Members.Count} 位成员";
            MemberHeader.Text = $"{dept.Members.Count} 位成员";

            ApplyRemark(dept.Remark);
            BuildMembers(dept.Members);

            // 抽屉里已经在部门里的人要显示成勾上的
            Picker.SetChecked(dept.Members);
        }


        // 描述为空时显示绿色的"添加部门描述", 与主流 IM 一致 -- 空着不留白行
        private void ApplyRemark(string? remark)
        {
            if (string.IsNullOrWhiteSpace(remark))
            {
                RemarkText.Text = "添加部门描述";
                RemarkText.Foreground = new SolidColorBrush(Color.Parse("#43A047"));
            }
            else
            {
                RemarkText.Text = remark;
                RemarkText.Foreground = new SolidColorBrush(Color.Parse("#1E1E1E"));
            }
        }


        // ---------------- 成员行 ----------------

        private void BuildMembers(IEnumerable<string> members)
        {
            MemberList.Children.Clear();
            foreach (var nick in members)
            {
                MemberList.Children.Add(BuildMemberRow(nick));
            }
        }


        private Control BuildMemberRow(string nickname)
        {
            // 签名从联系人表里查; 查不到(已不是联系人)就留空, 不编造内容
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

            // 移除按钮: 红圆 + 白横杠.显示/隐藏由 XAML 的 Border.row:pointerover 管
            var remove = new Button
            {
                VerticalAlignment = VerticalAlignment.Center,
                Content = new Avalonia.Controls.Shapes.Rectangle
                {
                    Width = 12,
                    Height = 2,
                    RadiusX = 1,
                    RadiusY = 1,
                    Fill = Brushes.White,
                },
            };
            remove.Classes.Add("removeBtn");
            ToolTip.SetTip(remove, "移出本部门");
            remove.Click += (_, _) => ConfirmRemove(nickname);

            var grid = new Grid
            {
                ColumnDefinitions = new ColumnDefinitions("Auto,*,Auto"),
            };
            Grid.SetColumn(avatar, 0);
            Grid.SetColumn(texts, 1);
            Grid.SetColumn(remove, 2);
            grid.Children.Add(avatar);
            grid.Children.Add(texts);
            grid.Children.Add(remove);

            var row = new Border { Child = grid };
            row.Classes.Add("row");   // 复用 XAML 里 Border.row 的高度/hover
            return row;
        }


        // 移人是不可撤销的动作, 先问一句再抛给宿主
        private async void ConfirmRemove(string nickname)
        {
            if (dept == null)
            {
                return;
            }

            // 存一份: 等确认框的这段时间里用户可能已经切到别的部门了
            var target = dept;

            bool ok = await MessageBoxWindow.Confirm(
                this,
                "移除成员",
                $"确定把 {nickname} 移出 {target.DeptName} 吗?",
                okText: "移除",
                danger: true);

            if (ok)
            {
                MemberToggled?.Invoke(target, nickname, false);
            }
        }


        // ---------------- 动作 ----------------

        private void Back_Click(object? sender, RoutedEventArgs e)
        {
            BackRequested?.Invoke();
        }

        private void RenameDept_Click(object? sender, RoutedEventArgs e)
        {
            if (dept != null) RenameRequested?.Invoke(dept);
        }

        private void EditRemark_Click(object? sender, RoutedEventArgs e)
        {
            if (dept != null) RemarkEditRequested?.Invoke(dept);
        }

        // 顶部圆按钮"添加"和成员区的"添加成员"是同一件事: 推出右侧选人抽屉
        private void AddMember_Click(object? sender, RoutedEventArgs e)
        {
            if (dept != null) Picker.Toggle();
        }

        private void AddMemberRow_PointerPressed(object? sender, PointerPressedEventArgs e)
        {
            if (dept != null) Picker.Toggle();
        }

        // 抽屉里勾/取消一个人: 本控件不动数据, 交给宿主去发请求, 成功后由宿主调 Refresh()
        private void OnPickerToggled(string nickname, bool on)
        {
            if (dept != null) MemberToggled?.Invoke(dept, nickname, on);
        }

        // 免打扰只是本地开关, 不需要服务端, 所以就地处理
        private void Mute_Click(object? sender, RoutedEventArgs e)
        {
            bool muted = MuteState.Text == "始终静音";
            MuteState.Text = muted ? "接收全部消息" : "始终静音";
        }

        private void Search_Click(object? sender, RoutedEventArgs e)
        {
            if (dept != null) SearchRequested?.Invoke(dept);
        }
    }
}
