using Avalonia.Controls;
using CC.Model;
using System;
using System.Collections.Generic;
using System.Linq;

namespace CC
{
    public partial class OrgTab : BaseTab
    {
        public event Action<string>? SendRequested;

        public OrgTab()
        {
            InitializeComponent();
            OrgPanel.DeptSelected += OpenDept;
            OrgPanel.AddOrgRequested += OnAddOrg;
            ChatView.SendRequested += t => SendRequested?.Invoke(t);

            OrgEditView.Cancelled += ShowEmptyState;

            OrgPanel.AddEmployeeRequested += OnAddEmployee;
            MemberEditView.Cancelled += ShowEmptyState;

            ChatView.DetailRequested += ShowDetail;
            ChatView.SearchRequested += () => { if (current != null) OpenSearch(current); };

            OrgDetailView.BackRequested += BackToChat;
            OrgDetailView.SearchRequested += OpenSearch;
            OrgDetailView.MemberToggled += OnDeptMemberToggled;

            SearchView.BackRequested += ShowDetail;
        }

        private OrgItem? current;


        protected override void OnNotify(Message msg)
        {
            switch (msg.ID)
            {
                case MsgID.DeptCreated:
                    OnDeptCreated(msg.Param as Department);
                    break;

                case MsgID.MemberCreated:
                    OnMemberCreated(msg.Param as User);
                    break;
            }
        }


        private void OnMemberCreated(User? user)
        {
            if (user != null && !OrgPanel.AllMembers.Members.Any(u => u.Username == user.Username))
            {
                OrgPanel.AllMembers.Members.Add(user);
            }

            OrgPanel.Select(OrgPanel.AllMembers);
        }


        private void OnDeptCreated(Department? dept)
        {
            if (dept == null)
            {
                return;
            }

            var item = OrgPanel.AddDept(dept);
            OrgPanel.Select(item);
        }

        private void OpenDept(OrgItem item)
        {
            current = item;
            ChatView.PeerName = item.DeptName;
            ChatView.PeerStatus = $"{item.Members.Count} 名成员";
            ChatView.ClearMessages();
            ShowOnly(ChatView);
        }


        private void ShowDetail()
        {
            if (current == null)
            {
                return;
            }

            OrgDetailView.Show(current);
            ShowOnly(OrgDetailView);
        }


        private void BackToChat()
        {
            ShowOnly(current != null ? ChatView : EmptyState);
        }


        private void OnDeptMemberToggled(OrgItem dept, User user, bool joined)
        {
            if (joined)
            {
                if (!dept.Members.Any(u => u.Nickname == user.Nickname))
                {
                    dept.Members.Add(user);
                }
            }
            else
            {
                dept.Members.Remove(user);
            }

            OrgDetailView.Refresh();
            if (ReferenceEquals(dept, current))
            {
                ChatView.PeerStatus = $"{dept.Members.Count} 名成员";
            }
        }

        private void OpenSearch(OrgItem item)
        {
            SearchView.Show(item.DeptName);
            ShowOnly(SearchView);
        }

        public void AddText(bool mine, string text, string time)
        {
            ChatView.AddText(mine, text, time);
        }

        private void OnAddOrg()
        {
            OrgEditView.Reset();
            ShowOnly(OrgEditView);
        }

        private void OnAddEmployee()
        {
            MemberEditView.SetDepartments(OrgPanel.DeptNames());
            MemberEditView.Reset();
            ShowOnly(MemberEditView);
        }


        private void ShowEmptyState()
        {
            ShowOnly(EmptyState);
        }


        private void ShowOnly(Control target)
        {
            ChatView.IsVisible       = target == ChatView;
            OrgEditView.IsVisible    = target == OrgEditView;
            MemberEditView.IsVisible = target == MemberEditView;
            OrgDetailView.IsVisible  = target == OrgDetailView;
            SearchView.IsVisible     = target == SearchView;
            EmptyState.IsVisible     = target == EmptyState;
        }
    }
}
