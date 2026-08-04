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

        // 当前选中的部门项
        private OrgItem? selectedItem;

        public const long AllMembersID = 0;

        /// <summary>
        /// 常驻的"所有成员"
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
                AddDept(new Department { ID = id, Name = name },
                        members.Select(n => new User { Nickname = n }),
                        last, time, unread);
            }
        }


        public OrgItem AddDept(Department dept, IEnumerable<User>? members = null,
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
