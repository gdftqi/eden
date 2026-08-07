using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Media.Transformation;
using Avalonia.Threading;
using CC.Eva;
using CC.Model;
using Lilith.Utils;
using System;
using System.Collections.Generic;
using System.Linq;

namespace CC
{
    public partial class OrgMemberEditPanel : UserControl
    {
        // 点"取消"时触发: 关掉本页之后显示什么, 是宿主的事
        public event Action? Cancelled;

        // 抽屉宽度, 必须与 XAML 里 DeptDrawer 的 Width 一致(关闭时要正好平移出右边界)
        private const double DRAWER_W = 360;

        // 与 XAML 里 TransformOperationsTransition 的 Duration 对齐, 略留一点余量
        private static readonly TimeSpan DRAWER_MS = TimeSpan.FromMilliseconds(260);

        // 可选的全部部门
        private readonly List<Department> allDepts = new();

        // 已勾选的部门(按 id -- 建成员时要把这些 id 发上去)
        private readonly HashSet<Int64> pickedDepts = new();

        // 部门 id -> 抽屉里那一行的复选框
        private readonly Dictionary<Int64, CheckBox> deptChecks = new();

        public OrgMemberEditPanel()
        {
            InitializeComponent();
            Reset();
        }


        /// <summary>
        /// 换一份可选部门.没有 id 的会被丢掉: 勾选和上报都以 id 为准.
        /// </summary>
        public void SetDepartments(IEnumerable<Department> depts)
        {
            allDepts.Clear();
            allDepts.AddRange(depts.Where(d => d.ID.HasValue));
            BuildDeptList();
        }


        public void Reset()
        {
            NameBox.Text = string.Empty;
            PassBox.Text = string.Empty;
            PhoneBox.Text = string.Empty;

            pickedDepts.Clear();
            DeptSearch.Text = string.Empty;
            BuildDeptList();
            ApplyDeptSummary();

            NameBox.Focus();
        }


        // ---------------- 所属部门(右侧抽屉多选) ----------------

        private void BuildDeptList(string keyword = "")
        {
            DeptList.Children.Clear();
            deptChecks.Clear();

            var hits = keyword.Length == 0
                ? allDepts
                : allDepts.Where(d => (d.Name ?? string.Empty).Contains(keyword, StringComparison.OrdinalIgnoreCase)).ToList();

            if (hits.Count == 0)
            {
                DeptList.Children.Add(new TextBlock
                {
                    Text = allDepts.Count == 0 ? "还没有任何部门" : "没有匹配的部门",
                    FontSize = 13,
                    Foreground = new SolidColorBrush(Color.Parse("#9AA0A6")),
                    HorizontalAlignment = HorizontalAlignment.Center,
                    Margin = new Thickness(0, 22, 0, 22),
                });
                return;
            }

            foreach (var d in hits)
            {
                DeptList.Children.Add(BuildDeptRow(d));
            }
        }


        private Control BuildDeptRow(Department dept)
        {
            // SetDepartments 已经把没有 id 的滤掉了, 这里的都有
            var id = dept.ID!.Value;

            var check = new CheckBox
            {
                IsChecked = pickedDepts.Contains(id),
                IsHitTestVisible = false,
                VerticalAlignment = VerticalAlignment.Center,
                Margin = new Thickness(0, 0, 6, 0),
            };
            deptChecks[id] = check;

            var icon = new Border
            {
                Width = 30,
                Height = 30,
                CornerRadius = new CornerRadius(15),
                Background = new SolidColorBrush(Color.Parse("#E4E7EB")),
                Margin = new Thickness(0, 0, 10, 0),
                Child = new Viewbox
                {
                    Width = 16,
                    Height = 16,
                    Child = new Canvas
                    {
                        Width = 24,
                        Height = 24,
                        Children =
                        {
                            new Avalonia.Controls.Shapes.Path
                            {
                                Fill = new SolidColorBrush(Color.Parse("#5F6368")),
                                Data = Geometry.Parse("M12 7V3H2v18h20V7H12zM6 19H4v-2h2v2zm0-4H4v-2h2v2zm0-4H4V9h2v2zm0-4H4V5h2v2zm4 12H8v-2h2v2zm0-4H8v-2h2v2zm0-4H8V9h2v2zm0-4H8V5h2v2zm10 12h-8v-2h2v-2h-2v-2h2v-2h-2V9h8v10zm-2-8h-2v2h2v-2zm0 4h-2v2h2v-2z"),
                            },
                        },
                    },
                },
            };

            var name = new TextBlock
            {
                Text = dept.Name ?? string.Empty,
                FontSize = 13,
                Foreground = new SolidColorBrush(Color.Parse("#1E1E1E")),
                VerticalAlignment = VerticalAlignment.Center,
                TextTrimming = TextTrimming.CharacterEllipsis,
            };

            var row = new StackPanel { Orientation = Orientation.Horizontal };
            row.Children.Add(check);
            row.Children.Add(icon);
            row.Children.Add(name);

            var body = new Border
            {
                Height = 44,
                Padding = new Thickness(10, 0),
                Background = Brushes.Transparent,
                Cursor = new Cursor(StandardCursorType.Hand),
                Child = row,
            };
            body.PointerPressed += (_, _) => ToggleDept(id);
            return body;
        }


        private void ToggleDept(Int64 id)
        {
            if (!pickedDepts.Remove(id))
            {
                pickedDepts.Add(id);
            }

            if (deptChecks.TryGetValue(id, out var cb))
            {
                cb.IsChecked = pickedDepts.Contains(id);
            }

            ApplyDeptSummary();
        }


        private void ApplyDeptSummary()
        {
            if (pickedDepts.Count == 0)
            {
                DeptSummary.Text = "请选择部门";
                DeptSummary.Foreground = new SolidColorBrush(Color.Parse("#9AA0A6"));
                return;
            }

            // 按 allDepts 的顺序拼, 不按勾选先后 -- 摘掉再勾上不该让整串重排
            DeptSummary.Text = string.Join("、", allDepts
                .Where(d => pickedDepts.Contains(d.ID!.Value))
                .Select(d => d.Name ?? string.Empty));
            DeptSummary.Foreground = new SolidColorBrush(Color.Parse("#1E1E1E"));
        }


        private void DeptSearch_TextChanged(object? sender, TextChangedEventArgs e)
        {
            BuildDeptList((DeptSearch.Text ?? string.Empty).Trim());
        }


        private void OpenDeptPicker_PointerPressed(object? sender, PointerPressedEventArgs e)
        {
            DeptDrawer.IsVisible = true;

            Dispatcher.UIThread.Post(
                () => DeptDrawer.RenderTransform = TransformOperations.Parse("translateX(0px)"),
                DispatcherPriority.Render);

            DeptSearch.Focus();
        }


        private void CloseDeptPicker_Click(object? sender, RoutedEventArgs e)
        {
            DeptDrawer.RenderTransform = TransformOperations.Parse($"translateX({DRAWER_W}px)");
            DispatcherTimer.RunOnce(() => DeptDrawer.IsVisible = false, DRAWER_MS);
        }


        // ---------------- 提交 ----------------

        private async void Save_Click(object? sender, RoutedEventArgs e)
        {
            var name = (NameBox.Text ?? string.Empty).Trim();
            if (name.Length < 5 || name.Length > 16)
            {
                Tips.Error("请填写用户名");
                return;
            }

            var pass = PassBox.Text ?? string.Empty;
            if (pass.Length < 8 || pass.Length > 16)
            {
                Tips.Error("请填写密码");
                return;
            }
            pass = Crypto.Sha256(pass);

            var phoneNum = PhoneBox.Text ?? string.Empty;
            var nickname = string.Format("成员 {0}", Random.Shared.NextInt64(10000));

            try
            {
                await CreateUser.POST(name, pass, nickname, phoneNum, pickedDepts.ToList());
            }
            catch (Exception ex)
            {
                Tips.Error(ex.Message);
                return;
            }

            Tips.Success("添加成员成功");
            BaseTab.Notify<OrgTab>(new Message(MsgID.MemberCreated,
                new User { Username = name, Nickname = nickname, PhoneNum = phoneNum }, this));

            Reset();
        }


        private void Cancel_Click(object? sender, RoutedEventArgs e)
        {
            Cancelled?.Invoke();
        }
    }
}
