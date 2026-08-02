using Avalonia.Controls;
using Avalonia.Interactivity;
using System;
using System.Collections.Generic;
using System.Linq;

namespace CC
{
    // 组织架构面板: 标题 + 搜索栏 + 添加部门 + 部门列表.
    // 部门是平铺的(不展开人员) -- 一个部门就是一个群聊, 点中即抛 DeptSelected 给宿主开聊天窗.
    public partial class OrgListPanel : UserControl
    {
        // 选中某个部门时触发(宿主据此打开右侧群聊界面)
        public event Action<OrgItem>? DeptSelected;

        // 点了"创建部门"时触发(宿主决定弹窗还是切界面)
        public event Action? AddOrgRequested;

        // 点了"创建成员"时触发
        public event Action? AddEmployeeRequested;

        // 当前选中的部门项(高亮由 IsSelected 驱动, 不依赖焦点)
        private OrgItem? selectedItem;

        public OrgListPanel()
        {
            InitializeComponent();
            LoadSampleDepts();
        }

        // 临时: 示例部门(以后换成真实数据)
        private void LoadSampleDepts()
        {
            (string dept, string[] members, string last, string time, int unread)[] data =
            {
                ("研发部", new[] { "美女1", "联系人 1", "联系人 2" }, "老王: 今晚的版本先别发", "17:20", 3),
                ("市场部", new[] { "美女2", "美女3" },                "小李: 方案已经发群里了", "16:02", 0),
                ("行政部", new[] { "联系人 3" },                      "本周五团建, 记得报名",   "昨天",  0),
            };

            foreach (var (dept, members, last, time, unread) in data)
            {
                AddDept(dept, string.Empty, members, last, time, unread);
            }
        }


        /// <summary>
        /// 追加一个部门到列表末尾.供宿主在"创建部门"成功后调用.
        /// </summary>
        public OrgItem AddDept(string dept, string remark = "", IEnumerable<string>? members = null,
                               string last = "", string time = "", int unread = 0)
        {
            var item = new OrgItem
            {
                DeptName    = dept,
                Remark      = remark,
                LastMessage = last,
                Time        = time,
                Unread      = unread,
            };

            if (members != null)
            {
                foreach (var m in members)
                {
                    item.Members.Add(m);
                }
            }

            item.PointerPressed += (_, _) => Select(item);
            OrgList.Children.Add(item);
            return item;
        }


        /// <summary>
        /// 当前所有部门项.列表是这里的私有结构, 外面要用就走这个口.
        /// </summary>
        public IEnumerable<OrgItem> Depts()
        {
            return OrgList.Children.OfType<OrgItem>();
        }


        /// <summary>
        /// 部门名列表, 供"创建成员"的部门下拉用.
        /// </summary>
        public IEnumerable<string> DeptNames()
        {
            return Depts().Select(d => d.DeptName ?? string.Empty).Where(n => n.Length > 0);
        }


        /// <summary>
        /// 按名字找部门, 找不到返回 null.
        /// </summary>
        public OrgItem? FindDept(string name)
        {
            return Depts().FirstOrDefault(d => d.DeptName == name);
        }


        // 切换选中部门: 旧的熄灭, 新的点亮, 再通知宿主
        private void Select(OrgItem item)
        {
            if (selectedItem != null)
            {
                selectedItem.IsSelected = false;
            }
            selectedItem = item;
            item.IsSelected = true;
            DeptSelected?.Invoke(item);
        }

        private void AddOrg_Click(object? sender, RoutedEventArgs e)
        {
            AddOrgRequested?.Invoke();
        }

        private void AddEmployee_Click(object? sender, RoutedEventArgs e)
        {
            AddEmployeeRequested?.Invoke();
        }

        private void ClearSearch_Click(object? sender, RoutedEventArgs e)
        {// 搜索框 清空文字
            SearchBox.Text = string.Empty;
            SearchBox.Focus();
        }
    }
}
