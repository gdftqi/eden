using Avalonia.Controls;
using Avalonia.Interactivity;
using System;
using System.Collections.Generic;
using System.Linq;
using CC.Model;

namespace CC
{
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

        /// <summary>
        /// "所有成员"这个常驻项占用的 id.真部门的 id 由 MySQL 自增, 从 1 起,
        /// 所以 0 空着可以拿来当哨兵, 不会和任何真部门撞上.
        /// </summary>
        public const long AllMembersID = 0;

        /// <summary>
        /// 常驻的"所有成员".不属于任何部门, 也不参与部门列表的加载与刷新.
        /// </summary>
        public OrgItem AllMembers => AllMembersItem;

        public OrgListPanel()
        {
            InitializeComponent();

            AllMembersItem.Dept = new Department { ID = AllMembersID, Name = "所有成员" };
            AllMembersItem.PointerPressed += (_, _) => Select(AllMembersItem);

            LoadSampleDepts();
        }

        // 临时: 示例部门(以后换成真实数据)
        private void LoadSampleDepts()
        {
            (long id, string name, string[] members, string last, string time, int unread)[] data =
            {
                (1, "研发部", new[] { "美女1", "联系人 1", "联系人 2" }, "老王: 今晚的版本先别发", "17:20", 3),
                (2, "市场部", new[] { "美女2", "美女3" },                "小李: 方案已经发群里了", "16:02", 0),
                (3, "行政部", new[] { "联系人 3" },                      "本周五团建, 记得报名",   "昨天",  0),
            };

            foreach (var (id, name, members, last, time, unread) in data)
            {
                AddDept(new Department { ID = id, Name = name }, members, last, time, unread);
            }
        }


        /// <summary>
        /// 追加一个部门到列表末尾.供宿主在"创建部门"成功后调用.
        /// </summary>
        public OrgItem AddDept(Department dept, IEnumerable<string>? members = null,
                               string last = "", string time = "", int unread = 0)
        {
            var item = new OrgItem
            {
                Dept        = dept,
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


        public IEnumerable<OrgItem> Depts()
        {
            return OrgList.Children.OfType<OrgItem>();
        }


        /// <summary>
        /// 部门名列表, 供"创建成员"的部门下拉用.
        /// </summary>
        public IEnumerable<string> DeptNames()
        {
            return Depts().Select(d => d.DeptName).Where(n => n.Length > 0);
        }


        public OrgItem? FindDept(long id)
        {
            return Depts().FirstOrDefault(d => d.Dept?.ID == id);
        }


        public void Select(OrgItem item)
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
        {
            SearchBox.Text = string.Empty;
            SearchBox.Focus();
        }
    }
}
