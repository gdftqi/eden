using Avalonia.Controls;
using System;
using System.Collections.Generic;

namespace CC
{
    // 组织架构页: OrgListPanel + 右侧群聊窗.
    // 一个部门就是一个群聊, 所以右侧用 MultiChatWindow, 与 ChatTab 同构.
    public partial class OrgTab : UserControl
    {
        // 群聊里点发送时向外抛文字(网络收发在 MainWindow, 这里只管界面)
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
            // 顶栏的搜索按钮: 和详情里的搜索是同一件事
            ChatView.SearchRequested += () => { if (current != null) OpenSearch(current); };

            OrgDetailView.BackRequested += BackToChat;
            OrgDetailView.SearchRequested += OpenSearch;
            OrgDetailView.MemberToggled += OnDeptMemberToggled;

            // 搜索是从详情进来的, 返回就回详情
            SearchView.BackRequested += ShowDetail;
        }

        // 当前打开的部门(详情面板要用它的成员/备注)
        private OrgItem? current;

        // 点中某个部门: 右侧切成该部门的群聊
        private void OpenDept(OrgItem item)
        {
            current = item;
            ChatView.PeerName = item.DeptName;
            ChatView.PeerStatus = $"{item.Members.Count} 名成员";

            // 临时: 还没有真实消息, 先清空(以后换成拉该部门的历史)
            ChatView.ClearMessages();
            ShowOnly(ChatView);
        }


        // 点了群聊顶栏: 打开群详情
        private void ShowDetail()
        {
            if (current == null)
            {
                return;
            }

            OrgDetailView.Show(current);
            ShowOnly(OrgDetailView);
        }


        // 详情里点返回: 回到刚才那个群聊(消息还在, 不重建)
        private void BackToChat()
        {
            ShowOnly(current != null ? ChatView : EmptyState);
        }


        // 详情页的选人抽屉里勾/取消了一个人
        private void OnDeptMemberToggled(OrgItem dept, string nickname, bool joined)
        {
            if (joined)
            {
                if (!dept.Members.Contains(nickname))
                {
                    dept.Members.Add(nickname);
                }
            }
            else
            {
                dept.Members.Remove(nickname);
            }

            // 只重画详情, 不碰抽屉 -- 用户通常要连着勾好几个
            OrgDetailView.Refresh();

            // 群聊顶栏那句"N 名成员"也得跟着变
            if (ReferenceEquals(dept, current))
            {
                ChatView.PeerStatus = $"{dept.Members.Count} 名成员";
            }
        }

        private void OpenSearch(OrgItem item)
        {
            SearchView.Show(item.DeptName ?? string.Empty);
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


        // 右侧六块是叠放的, 统一从这里切 -- 免得漏关其中一块导致两层同时可见
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
