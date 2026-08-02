using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Media.Transformation;
using Avalonia.Threading;
using System;
using System.Collections.Generic;
using System.Linq;

namespace CC
{
    /// <summary>
    /// 新建成员的表单内容.字段先按界面需要定, 等 Eva 的注册接口出来再对齐.
    /// </summary>
    public sealed class MemberDraft
    {
        public string Username { get; init; } = string.Empty;
        public string Password { get; init; } = string.Empty;

        /// <summary>所属部门.一个人可以在多个部门里, 所以是列表不是单值.</summary>
        public IReadOnlyList<string> Departments { get; init; } = Array.Empty<string>();

        public IReadOnlyList<string> Phones { get; init; } = Array.Empty<string>();
    }


    // 创建成员表单: 用户名 / 密码 / 所属部门 / 多个手机号.
    // 与 OrgEditPanel 同样的分工: 只管收集与校验, 存到哪去由宿主(OrgTab)决定.
    public partial class OrgMemberEditPanel : UserControl
    {
        // 点"确定"且校验通过时触发
        public event Action<MemberDraft>? Saved;

        // 点"取消"时触发
        public event Action? Cancelled;

        // 手机号最多几个.没有业务依据, 纯粹防止一直点 + 把界面撑爆.
        private const int MAX_PHONE = 5;

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
            ErrorTip.IsVisible = false;

            // 手机号回到"只有一行空的"
            PhoneList.Children.Clear();
            AddPhoneRow();

            pickedDepts.Clear();
            DeptSearch.Text = string.Empty;
            BuildDeptList();
            ApplyDeptSummary();
            HideDeptDrawerNow();

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
                // 只作显示: 点击由整行统一处理, 免得点方框和点空白行为不一致
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


        // 字段行上显示已选了什么; 一个没选就显示灰色的占位文案
        private void ApplyDeptSummary()
        {
            if (pickedDepts.Count == 0)
            {
                DeptSummary.Text = "请选择部门";
                DeptSummary.Foreground = new SolidColorBrush(Color.Parse("#9AA0A6"));
                return;
            }

            // 按 allDepts 的顺序拼, 免得每次点的顺序不同导致文案跳来跳去
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

            // 先让它以"在右边界之外"的状态完成一次布局, 下一帧再平移到位.
            // 同一帧里既显示又改变换的话, 过渡没有起点, 会直接跳出来而不是滑出来.
            Dispatcher.UIThread.Post(
                () => DeptDrawer.RenderTransform = TransformOperations.Parse("translateX(0px)"),
                DispatcherPriority.Render);

            DeptSearch.Focus();
        }


        private void CloseDeptPicker_Click(object? sender, RoutedEventArgs e)
        {
            DeptDrawer.RenderTransform = TransformOperations.Parse($"translateX({DRAWER_W}px)");

            // 等滑出动画播完再隐藏 -- 立刻置 IsVisible=false 是"瞬间消失", 看不到动画
            DispatcherTimer.RunOnce(() => DeptDrawer.IsVisible = false, DRAWER_MS);
        }


        // 不带动画地收起, 用于 Reset()
        private void HideDeptDrawerNow()
        {
            DeptDrawer.RenderTransform = TransformOperations.Parse($"translateX({DRAWER_W}px)");
            DeptDrawer.IsVisible = false;
        }


        // ---------------- 手机号 ----------------

        // 一行 = 输入框 + 删除按钮.只剩一行时不给删, 否则会变成一个也没有.
        private void AddPhoneRow(string value = "")
        {
            // 不设占位文字: 上面已经有"手机号"这个标题, 而且 XAML 里用的
            // PlaceholderText 与代码侧属性名是否一致我这边没法验证, 不冒这个险.
            var box = new TextBox
            {
                Text = value,
                MaxLength = 20,
            };
            box.Classes.Add("field");

            // 红底白横杠.删除按钮不参与布局(和输入框同格叠着) --
            // 占一列的话输入框会被挤窄, 就和上面"所属部门"不等宽了.
            var del = new Button
            {
                HorizontalAlignment = HorizontalAlignment.Right,
                VerticalAlignment = VerticalAlignment.Center,
                Margin = new Thickness(0, 0, 8, 0),
                IsVisible = false,
                Content = new Border
                {
                    Width = 10,
                    Height = 2,
                    CornerRadius = new CornerRadius(1),
                    Background = Brushes.White,
                },
            };
            del.Classes.Add("del");
            ToolTip.SetTip(del, "删除这个号码");

            // 同一格叠放: box 铺满, del 浮在右端
            var grid = new Grid();
            grid.Children.Add(box);
            grid.Children.Add(del);

            // 只有鼠标在这一行上,且不止一行时才露出删除
            grid.PointerEntered += (_, _) => del.IsVisible = PhoneList.Children.Count > 1;
            grid.PointerExited  += (_, _) => del.IsVisible = false;

            del.Click += (_, _) =>
            {
                PhoneList.Children.Remove(grid);
                ApplyPhoneState();
            };

            PhoneList.Children.Add(grid);
            ApplyPhoneState();
        }


        // 删除按钮平时一律收起(靠 hover 显示).删到只剩一行时也要收 --
        // 否则鼠标正停在那一行上, 会留着一个按得下去却不该按的删除.
        private void ApplyPhoneState()
        {
            foreach (var child in PhoneList.Children)
            {
                if (child is Grid g && g.Children.Count > 1 && g.Children[1] is Button del)
                {
                    del.IsVisible = false;
                }
            }

            AddPhoneBtn.IsVisible = PhoneList.Children.Count < MAX_PHONE;
        }


        private void AddPhone_Click(object? sender, RoutedEventArgs e)
        {
            if (PhoneList.Children.Count >= MAX_PHONE)
            {
                return;
            }

            AddPhoneRow();

            // 新加的那一行直接给焦点, 省一次点击
            if (PhoneList.Children[^1] is Grid g && g.Children.Count > 0 && g.Children[0] is TextBox box)
            {
                box.Focus();
            }
        }


        // 收集非空的手机号(去掉空行和重复)
        private List<string> CollectPhones()
        {
            var list = new List<string>();
            foreach (var child in PhoneList.Children)
            {
                if (child is Grid g && g.Children.Count > 0 && g.Children[0] is TextBox box)
                {
                    var t = (box.Text ?? string.Empty).Trim();
                    if (t.Length > 0 && !list.Contains(t))
                    {
                        list.Add(t);
                    }
                }
            }
            return list;
        }


        // ---------------- 提交 ----------------

        private void Save_Click(object? sender, RoutedEventArgs e)
        {
            var name = (NameBox.Text ?? string.Empty).Trim();
            if (name.Length == 0)
            {
                ShowError("请填写用户名");
                return;
            }

            var pass = PassBox.Text ?? string.Empty;
            if (pass.Length == 0)
            {
                ShowError("请填写密码");
                return;
            }

            if (pickedDepts.Count == 0)
            {
                ShowError("请选择所属部门");
                return;
            }

            ErrorTip.IsVisible = false;
            Saved?.Invoke(new MemberDraft
            {
                Username    = name,
                Password    = pass,
                // 按 allDepts 的顺序输出, 结果与界面上看到的一致
                Departments = allDepts.Where(pickedDepts.Contains).ToList(),
                Phones      = CollectPhones(),
            });
        }


        private void Cancel_Click(object? sender, RoutedEventArgs e)
        {
            Cancelled?.Invoke();
        }


        /// <summary>
        /// 显示一条错误提示.公开出去是为了让宿主也能用 --
        /// "用户名已存在"这类只有服务端知道的错误要回显在同一个位置.
        /// </summary>
        public void ShowError(string msg)
        {
            ErrorTip.Text = msg;
            ErrorTip.IsVisible = true;
        }
    }
}
