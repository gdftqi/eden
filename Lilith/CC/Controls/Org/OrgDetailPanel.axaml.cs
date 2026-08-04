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
        public event Action<OrgGroup>? RenameRequested;
        public event Action<OrgGroup>? RemarkEditRequested;
        public event Action<OrgGroup, User, bool>? MemberToggled;
        public event Action<OrgGroup>? SearchRequested;

        // 当前展示的部门
        private OrgGroup? dept;

        public OrgDetailPanel()
        {
            InitializeComponent();
            Picker.Toggled += OnPickerToggled;
        }


        public void Show(OrgGroup item)
        {
            dept = item;

            // 进页面时抽屉应当是关的: 上次离开时可能正开着
            Picker.HideNow();

            Refresh();
        }


        public void Refresh()
        {
            if (dept == null)
            {
                return;
            }

            DeptTitle.Text = dept.Dept?.Name ?? string.Empty;
            MemberSummary.Text = $"{dept.Members.Count} 位成员";
            MemberHeader.Text = $"{dept.Members.Count} 位成员";

            ApplyRemark(dept.Dept?.Desc);
            BuildMembers(dept.Members);

            Picker.SetChecked(dept.Members.Select(u => u.Nickname ?? string.Empty));
        }


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

        private void BuildMembers(IEnumerable<User> members)
        {
            MemberList.Children.Clear();
            foreach (var u in members)
            {
                MemberList.Children.Add(BuildMemberRow(u));
            }
        }


        private Control BuildMemberRow(User user)
        {
            var nickname = user.Nickname ?? string.Empty;

            var sign = user.Username ?? string.Empty;

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
            remove.Click += (_, _) => ConfirmRemove(user);

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
            row.Classes.Add("row");
            return row;
        }


        // 移人是不可撤销的动作, 先问一句再抛给宿主
        private async void ConfirmRemove(User user)
        {
            if (dept == null)
            {
                return;
            }

            var target = dept;

            bool ok = await MessageBoxWindow.Confirm(
                this,
                "移除成员",
                $"确定把 {user.Nickname} 移出 {target.Dept?.Name} 吗?",
                okText: "移除",
                danger: true);

            if (ok)
            {
                MemberToggled?.Invoke(target, user, false);
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

  
        private void AddMember_Click(object? sender, RoutedEventArgs e)
        {
            if (dept != null) Picker.Toggle();
        }

        private void AddMemberRow_PointerPressed(object? sender, PointerPressedEventArgs e)
        {
            if (dept != null) Picker.Toggle();
        }

        private void OnPickerToggled(string nickname, bool on)
        {
            if (dept == null)
            {
                return;
            }

 
            var user = dept.Members.FirstOrDefault(u => u.Nickname == nickname)
                       ?? new User { Nickname = nickname };

            MemberToggled?.Invoke(dept, user, on);
        }


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
