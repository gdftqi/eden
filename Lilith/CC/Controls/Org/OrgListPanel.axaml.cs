using Avalonia.Controls;
using Avalonia.Interactivity;
using CC.Model;
using System;
using System.Collections.Generic;
using System.Linq;

namespace CC
{
    public partial class OrgListPanel : UserControl
    {
        public event Action<OrgGroup>? GroupChatSelected;
        public event Action<User>? MemberSelected;
        public event Action? AddOrgRequested;
        public event Action? AddEmployeeRequested;

        private OrgItem? selectedRow;

        public const long AllMembersID = 0;

        public OrgGroup AllMembers => AllMembersGroup;

        public OrgListPanel()
        {
            InitializeComponent();

            AllMembersGroup.Bind(new Department { ID = AllMembersID, Name = "所有成员" },
                                 null, hasGroupChat: false);
            Hook(AllMembersGroup);

            LoadSampleDepts();
        }

        // 临时: 示例部门(以后换成拉服务端)
        private void LoadSampleDepts()
        {
            (long id, string name, string[] members)[] data =
            {
                (1, "研发部", new[] { "美女1", "联系人 1", "联系人 2" }),
                (2, "市场部", new[] { "美女2", "美女3" }),
                (3, "行政部", new[] { "联系人 3" }),
            };

            foreach (var (id, name, members) in data)
            {
                AddDept(new Department { ID = id, Name = name },
                        members.Select(n => new User { Nickname = n }));
            }
        }


        public OrgGroup AddDept(Department dept, IEnumerable<User>? members = null)
        {
            var group = new OrgGroup();
            group.Bind(dept, members);
            Hook(group);

            OrgList.Children.Add(group);
            return group;
        }


        private void Hook(OrgGroup group)
        {
            group.GroupChatSelected += g =>
            {
                if (g.GroupChatItem != null) Select(g.GroupChatItem);
                GroupChatSelected?.Invoke(g);
            };

            group.MemberSelected += (row, user) =>
            {
                Select(row);
                MemberSelected?.Invoke(user);
            };
        }


        // 只含真部门, 不含"所有成员"
        public IEnumerable<OrgGroup> Groups()
        {
            return OrgList.Children.OfType<OrgGroup>();
        }


        public IEnumerable<string> DeptNames()
        {
            return Groups().Select(g => g.Dept?.Name ?? string.Empty).Where(n => n.Length > 0);
        }


        public OrgGroup? FindDept(long id)
        {
            return Groups().FirstOrDefault(g => g.Dept?.ID == id);
        }


        public void Select(OrgItem row)
        {
            if (selectedRow != null)
            {
                selectedRow.IsSelected = false;
            }

            selectedRow = row;
            row.IsSelected = true;
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
