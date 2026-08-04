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
    /// <summary>
    /// 新建成员的表单内容.字段先按界面需要定, 等 Eva 的注册接口出来再对齐.
    /// </summary>
    public sealed class MemberDraft
    {
        public string Username { get; init; } = string.Empty;
        public string Password { get; init; } = string.Empty;

        /// <summary>所属部门.一个人可以在多个部门里, 所以是列表不是单值.</summary>
        public IReadOnlyList<string> Departments { get; init; } = Array.Empty<string>();

        /// <summary>手机号. 只支持一个 -- t_user_info.f_phone_num 是单列且带 UNIQUE.</summary>
        public string Phone { get; init; } = string.Empty;
    }


    // 创建成员表单: 用户名 / 密码 / 所属部门 / 多个手机号.
    // 与 OrgEditPanel 同样的分工: 只管收集与校验, 存到哪去由宿主(OrgTab)决定.
    public partial class OrgMemberEditPanel : UserControl
    {
        // 点"确定"且校验通过时触发
        public event Action<MemberDraft>? Saved;

        // 点"取消"时触发
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
            ErrorTip.IsVisible = false;

            PhoneBox.Text = string.Empty;

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


        // ---------------- 提交 ----------------

        private async void Save_Click(object? sender, RoutedEventArgs e)
        {
            var name = (NameBox.Text ?? string.Empty).Trim();
            if (name.Length < 5 || name.Length > 16)
            {
                ShowError("请填写用户名");
                return;
            }

            var pass = PassBox.Text ?? string.Empty;
            if (pass.Length < 8 || pass.Length > 16)
            {
                ShowError("请填写密码");
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
                ShowError(ex.Message);
                return;
            }

            ErrorTip.IsVisible = false;
            Saved?.Invoke(new MemberDraft
            {
                Username = name,
                Password = pass,
                Departments = allDepts.Where(pickedDepts.Contains).ToList(),
                Phone = phoneNum,
            });
            Clear();
        }


        private void Cancel_Click(object? sender, RoutedEventArgs e)
        {
            Cancelled?.Invoke();
        }


        public void ShowError(string msg)
        {
            ErrorTip.Text = msg;
            ErrorTip.IsVisible = true;
        }


        private void Clear()
        {
            PassBox.Text = NameBox.Text = PhoneBox.Text = "";
            //DeptPicker.
        }
    }
}
