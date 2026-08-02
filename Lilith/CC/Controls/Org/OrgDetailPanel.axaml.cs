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
    // 部门信息(群详情).只负责呈现, 数据由宿主用 Show(item) 灌进来;
    // 需要改数据的动作一律抛事件, 由宿主决定怎么做.
    public partial class OrgDetailPanel : UserControl
    {
        public event Action? BackRequested;

        // 重命名部门 / 编辑部门描述 / 添加成员 -- 都要落到服务端, 本控件不自己改
        public event Action<OrgItem>? RenameRequested;
        public event Action<OrgItem>? RemarkEditRequested;
        public event Action<OrgItem>? AddMemberRequested;

        // 搜索本部门的消息
        public event Action<OrgItem>? SearchRequested;

        // 当前展示的部门
        private OrgItem? dept;

        public OrgDetailPanel()
        {
            InitializeComponent();
        }


        /// <summary>
        /// 用某个部门的数据填充.每次打开都调, 不缓存 --
        /// 部门信息随时可能被别处改掉, 缓存了就会显示过期内容.
        /// </summary>
        public void Show(OrgItem item)
        {
            dept = item;

            DeptTitle.Text = item.DeptName;
            MemberSummary.Text = $"{item.Members.Count} 位成员";
            MemberHeader.Text = $"{item.Members.Count} 位成员";

            ApplyRemark(item.Remark);
            BuildMembers(item.Members);
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

            var grid = new Grid
            {
                ColumnDefinitions = new ColumnDefinitions("Auto,*"),
            };
            Grid.SetColumn(avatar, 0);
            Grid.SetColumn(texts, 1);
            grid.Children.Add(avatar);
            grid.Children.Add(texts);

            var row = new Border { Child = grid };
            row.Classes.Add("row");   // 复用 XAML 里 Border.row 的高度/hover
            return row;
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
            if (dept != null) AddMemberRequested?.Invoke(dept);
        }

        private void AddMemberRow_PointerPressed(object? sender, PointerPressedEventArgs e)
        {
            if (dept != null) AddMemberRequested?.Invoke(dept);
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
