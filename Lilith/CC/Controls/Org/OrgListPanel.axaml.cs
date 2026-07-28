using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Media;
using System;

namespace CC
{
    // 组织架构面板: 标题 + 搜索栏 + 添加部门 + 部门/员工列表;
    // 点部门(OrgItem)展开/收起它下面的员工(EmployeeItem), 选中员工抛 EmployeeSelected 给宿主
    public partial class OrgListPanel : UserControl
    {
        // 选中某个员工时触发(宿主据此打开右侧详情)
        public event Action<EmployeeItem>? EmployeeSelected;

        // 点了"添加部门"时触发(宿主决定弹窗还是切界面)
        public event Action? AddOrgRequested;

        // 当前选中的员工项(高亮由 IsSelected 驱动, 不依赖焦点)
        private EmployeeItem? selectedItem;

        public OrgListPanel()
        {
            InitializeComponent();
            LoadSampleOrgs();
        }

        // 临时: 示例部门/员工(以后换成真实数据)
        private void LoadSampleOrgs()
        {
            var avatar = new Avalonia.Media.Imaging.Bitmap(
                Avalonia.Platform.AssetLoader.Open(new Uri("avares://CC/Resources/unnamed.jpg")));

            (string dept, (string nick, string sign)[] members)[] data =
            {
                ("研发部", new[] { ("美女1", "今天也要加油鸭") }),
                ("市场部", new[] { ("美女2", ""), ("美女3", "忙, 勿扰") }),
                ("行政部", new[] { ("联系人 1", "这是一条示例签名"), ("联系人 2", "这是一条示例签名") }),
            };

            foreach (var (dept, members) in data)
            {
                AddOrg(avatar, dept, members);
            }
        }

        // 建一个部门 + 它的员工子列表(默认收起)
        private void AddOrg(IImage avatar, string dept, (string nick, string sign)[] members)
        {
            var sub = new StackPanel { IsVisible = false };
            foreach (var (nick, sign) in members)
            {
                var emp = new EmployeeItem { Avatar = avatar, Nickname = nick, Sign = sign };
                emp.PointerPressed += (_, _) => Select(emp);
                sub.Children.Add(emp);
            }

            var item = new OrgItem { DeptName = dept };
            item.PointerPressed += (_, _) =>
            {
                item.IsExpanded = !item.IsExpanded;
                sub.IsVisible = item.IsExpanded;
            };

            OrgList.Children.Add(item);
            OrgList.Children.Add(sub);
        }

        // 切换选中员工: 旧的熄灭, 新的点亮, 再通知宿主
        private void Select(EmployeeItem item)
        {
            if (selectedItem != null)
            {
                selectedItem.IsSelected = false;
            }
            selectedItem = item;
            item.IsSelected = true;
            EmployeeSelected?.Invoke(item);
        }

        private void AddOrg_Click(object? sender, RoutedEventArgs e)
        {
            AddOrgRequested?.Invoke();
        }

        private void ClearSearch_Click(object? sender, RoutedEventArgs e)
        {// 搜索框 清空文字
            SearchBox.Text = string.Empty;
            SearchBox.Focus();
        }
    }
}
