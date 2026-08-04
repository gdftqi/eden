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

            OrgEditView.Saved += OnDeptCreated;
            OrgEditView.Cancelled += ShowEmptyState;

            OrgPanel.AddEmployeeRequested += OnAddEmployee;
            MemberEditView.Saved += OnMemberCreated;
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


        // 详情里点搜索: 搜本部门的消息
        private void OpenSearch(OrgItem item)
        {
            SearchView.Show(item.DeptName ?? string.Empty);
            ShowOnly(SearchView);
        }

        // 服务端来的消息进当前群聊(MainWindow 转发过来)
        public void AddText(bool mine, string text, string time)
        {
            ChatView.AddText(mine, text, time);
        }

        // 点了左侧"添加部门": 右侧切成创建界面
        private void OnAddOrg()
        {
            OrgEditView.Reset();   // 清掉上次的残留
            ShowOnly(OrgEditView);
        }


        // 创建界面提交
        private void OnDeptCreated(string name, IReadOnlyList<string> members)
        {
            // TODO: 服务端还没有组织架构接口, 先只加到本地列表.
            // 接上之后这里改成: 发请求 -> 成功再 AddDept, 失败调 OrgEditView.ShowError(...)
            var item = OrgPanel.AddDept(name, string.Empty, members);

            // 建完直接进这个部门的群聊, 省得再点一次
            OpenDept(item);
        }


        // 点了左侧"创建成员": 右侧切成创建成员界面
        private void OnAddEmployee()
        {
            // 部门下拉的选项来自左侧列表 -- 部门归 OrgListPanel 所有, 表单不自己去拿
            MemberEditView.SetDepartments(OrgPanel.DeptNames());
            MemberEditView.Reset();
            ShowOnly(MemberEditView);
        }


        private void OnMemberCreated(MemberDraft draft)
        {
            // TODO: 服务端还没有创建用户的接口(Eva 那边), 现在只把人挂到所选的各个部门里.
            // 接上之后改成: 发请求 -> 成功再落地, 失败调 MemberEditView.ShowError(...)
            OrgItem? first = null;

            foreach (var name in draft.Departments)
            {
                var dept = OrgPanel.FindDept(name);
                if (dept == null)
                {
                    MemberEditView.ShowError($"找不到部门: {name}");
                    return;
                }

                if (!dept.Members.Contains(draft.Username))
                {
                    dept.Members.Add(draft.Username);
                }

                first ??= dept;
            }

            // 建完跳进第一个部门看效果
            if (first != null)
            {
                OpenDept(first);
            }

            // 页面已经跳走, 表单里那行提示用户根本来不及看见, 所以结果反馈交给顶部 Toast.
            // 只跳了第一个部门, 带上总数免得以为其余几个没加上
            int n = draft.Departments.Count;
            Toast(n > 1 ? $"已创建 {draft.Username}, 已加入 {n} 个部门" : $"已创建 {draft.Username}");
        }


        // 顶部那条一闪即收的提示归主窗所有, 沿用 MessageBoxWindow.Confirm 的反查写法.
        // 拿不到主窗就算了 -- 少一条提示而已, 不值得为它中断流程
        private void Toast(string text, bool ok = true)
        {
            (TopLevel.GetTopLevel(this) as MainWindow)?.ShowToast(text, ok);
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
