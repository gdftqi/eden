using Avalonia.Controls;
using CC.Model;
using System;
using System.Linq;

namespace CC
{
    public partial class OrgTab : BaseTab
    {
        public event Action<string>? SendRequested;

        public OrgTab()
        {
            InitializeComponent();

            OrgPanel.GroupChatSelected += OpenGroupChat;
            OrgPanel.MemberSelected += OpenSoloChat;
            OrgPanel.AddOrgRequested += OnAddOrg;
            OrgPanel.AddEmployeeRequested += OnAddEmployee;

            ChatView.SendRequested += t => SendRequested?.Invoke(t);
            SoloView.SendRequested += t => SendRequested?.Invoke(t);

            OrgEditView.Cancelled += ShowEmptyState;
            MemberEditView.Cancelled += ShowEmptyState;

            ChatView.DetailRequested += ShowDetail;
            ChatView.SearchRequested += () => { if (current != null) OpenSearch(current); };

            OrgDetailView.BackRequested += BackToChat;
            OrgDetailView.SearchRequested += OpenSearch;
            OrgDetailView.MemberToggled += OnDeptMemberToggled;

            SearchView.BackRequested += ShowDetail;
        }


        private OrgGroup? current;


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


        private void OnDeptCreated(Department? dept)
        {
            if (dept == null)
            {
                return;
            }

            var group = OrgPanel.AddDept(dept);
            group.Expand();

            if (group.GroupChatItem != null)
            {
                OrgPanel.Select(group.GroupChatItem);
                OpenGroupChat(group);
            }
        }


        private void OnMemberCreated(User? user)
        {
            if (user == null)
            {
                return;
            }

            var all = OrgPanel.AllMembers;
            if (!all.Members.Any(u => u.Username == user.Username))
            {
                all.Members.Add(user);
                all.Rebuild();
            }

            all.Expand();

            var row = all.RowOf(user);
            if (row != null)
            {
                OrgPanel.Select(row);
            }
            OpenSoloChat(user);
        }


        private void OpenGroupChat(OrgGroup group)
        {
            current = group;
            ChatView.PeerName = group.Dept?.Name;
            ChatView.PeerStatus = $"{group.Members.Count} 名成员";
            ChatView.ClearMessages();
            ShowOnly(ChatView);
        }


        private void OpenSoloChat(User user)
        {
            current = null;
            SoloView.PeerName = user.Nickname;
            SoloView.PeerStatus = user.Username;
            SoloView.PeerAvatar = ContactSource.Avatar;
            SoloView.ClearMessages();
            ShowOnly(SoloView);
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


        private void OnDeptMemberToggled(OrgGroup group, User user, bool joined)
        {
            if (joined)
            {
                if (!group.Members.Any(u => u.Nickname == user.Nickname))
                {
                    group.Members.Add(user);
                }
            }
            else
            {
                group.Members.Remove(user);
            }

            group.Rebuild();
            OrgDetailView.Refresh();

            if (ReferenceEquals(group, current))
            {
                ChatView.PeerStatus = $"{group.Members.Count} 名成员";
            }
        }


        private void OpenSearch(OrgGroup group)
        {
            SearchView.Show(group.Dept?.Name ?? string.Empty);
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
            SoloView.IsVisible       = target == SoloView;
            OrgEditView.IsVisible    = target == OrgEditView;
            MemberEditView.IsVisible = target == MemberEditView;
            OrgDetailView.IsVisible  = target == OrgDetailView;
            SearchView.IsVisible     = target == SearchView;
            EmptyState.IsVisible     = target == EmptyState;
        }
    }
}
