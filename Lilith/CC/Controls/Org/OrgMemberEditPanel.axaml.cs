using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Media.Transformation;
using Avalonia.Threading;
using CC.Eva;
using Lilith.Utils;
using System;
using System.Collections.Generic;
using System.Linq;

namespace CC
{
    // 创建成员表单: 用户名 / 密码 / 所属部门 / 手机号.
    // 校验、发请求、结果提示都在本控件里闭环, 不往宿主抛.
    public partial class OrgMemberEditPanel : UserControl
    {
        // 点"取消"时触发: 关掉本页之后显示什么, 是宿主的事
        public event Action? Cancelled;

        // 抽屉宽度, 必须与 XAML 里 DeptDrawer 的 Width 一致(关闭时要正好平移出右边界)
        private const double DRAWER_W = 360;

        // 与 XAML 里 TransformOperationsTransition 的 Duration 对齐, 略留一点余量
        private static readonly TimeSpan DRAWER_MS = TimeSpan.FromMilliseconds(260);

        // 可选的全部部门(宿主灌进来)
        private readonly List<string> allDepts = new();

        // 已勾选的部门
        private readonly HashSet<string> pickedDepts = new();

        // 部门名 -> 抽屉里那一行的复选框
        private readonly Dictionary<string, CheckBox> deptChecks = new();

        public OrgMemberEditPanel()
        {
            InitializeComponent();
            Reset();
        }


        /// <summary>
        /// 可选的部门.由宿主在打开面板前灌进来 --
        /// 部门列表归 OrgListPanel 所有, 本控件不该自己去拿.
        /// </summary>
        public void SetDepartments(IEnumerable<string> depts)
        {
            allDepts.Clear();
            allDepts.AddRange(depts);
            BuildDeptList();
        }


        /// <summary>
        /// 清空表单.每次打开都调一次, 否则会看到上一次没提交的残留内容.
        /// </summary>
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
                : allDepts.Where(d => d.Contains(keyword, StringComparison.OrdinalIgnoreCase)).ToList();

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


        private Control BuildDeptRow(string dept)
        {
            var check = new CheckBox
            {
                IsChecked = pickedDepts.Contains(dept),
                IsHitTestVisible = false,
                VerticalAlignment = VerticalAlignment.Center,
                Margin = new Thickness(0, 0, 6, 0),
            };
            deptChecks[dept] = check;

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
                Text = dept,
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
            body.PointerPressed += (_, _) => ToggleDept(dept);
            return body;
        }


        private void ToggleDept(string dept)
        {
            if (!pickedDepts.Remove(dept))
            {
                pickedDepts.Add(dept);
            }

            if (deptChecks.TryGetValue(dept, out var cb))
            {
                cb.IsChecked = pickedDepts.Contains(dept);
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

            DeptSummary.Text = string.Join("、", allDepts.Where(pickedDepts.Contains));
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

            try
            {
                await CreateUser.POST(name, pass, string.Format("成员 {0}", Random.Shared.NextInt64(10000)), phoneNum);
            }
            catch (Exception ex)
            {
                Tips.Error(ex.Message);
                return;
            }

            Tips.Success("添加成员成功");
            Reset();
        }


        private void Cancel_Click(object? sender, RoutedEventArgs e)
        {
            Cancelled?.Invoke();
        }
    }
}
