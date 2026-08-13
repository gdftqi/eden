using Avalonia;
using Avalonia.Controls;
using CC.Eva;
using CC.Model;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace CC
{
    public partial class OrgTab : BaseTab
    {
        public event Action<string>? SendRequested;

        public OrgTab()
        {
            InitializeComponent();

            OrgPanel.GroupChatSelected += OpenGroupChat;
            OrgPanel.MemberSelected += OpenMemberInfo;
            OrgPanel.AddOrgRequested += OnAddOrg;
            OrgPanel.AddEmployeeRequested += OnAddEmployee;

            ChatView.SendRequested += t => SendRequested?.Invoke(t);

            MemberView.CloseRequested += ShowEmptyState;
            MemberView.ChatRequested += OnMemberChat;

            OrgEditView.Cancelled += ShowEmptyState;
            MemberEditView.Cancelled += ShowEmptyState;

            ChatView.DetailRequested += ShowDetail;
            ChatView.SearchRequested += () => { if (current != null) OpenSearch(current); };

            OrgDetailView.BackRequested += BackToChat;
            OrgDetailView.SearchRequested += OpenSearch;
            OrgDetailView.MemberToggled += OnDeptMemberToggled;
            OrgDetailView.Changed += OnDeptChanged;

            SearchView.BackRequested += ShowDetail;
        }


        private OrgGroup? current;

        // 只拉一次. 四个页签都常驻在可视树里(靠 IsVisible 切), 进出会重复触发
        private bool loaded;


        protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
        {
            base.OnAttachedToVisualTree(e);

            if (!loaded)
            {
                loaded = true;
                _ = Reload();
            }
        }


        /// <summary>
        /// 重新拉组织架构.
        /// </summary>
        public async Task Reload()
        {
            GetOrg.Response rsp;

            try
            {
                rsp = await GetOrg.POST();
            }
            catch (Exception ex)
            {
                Tips.Error($"加载组织架构失败: {ex.Message}");
                return;
            }

            var users = rsp.Users ?? new List<User>();

            // 两个面板的选人抽屉都用这一份, 各自再去拉一次没必要
            OrgEditView.SetCandidates(users);
            OrgDetailView.SetCandidates(users);

            var index = new Dictionary<Int64, User>();
            foreach (var u in users)
            {
                if (u.ID.HasValue)
                {
                    index[u.ID.Value] = u;
                }
            }

            current = null;
            ShowOnly(EmptyState);

            OrgPanel.ClearDepts();
            foreach (var d in rsp.Departs ?? new List<Department>())
            {
                OrgPanel.AddDept(d, MembersOf(d, index));
            }

            var all = OrgPanel.AllMembers;
            all.Members.Clear();
            foreach (var u in users)
            {
                all.Members.Add(u);
            }
            all.Rebuild();
        }


        private static List<User> MembersOf(Department dept, Dictionary<Int64, User> index)
        {
            var list = new List<User>();

            foreach (var id in dept.UserIDs ?? new List<Int64>())
            {
                if (index.TryGetValue(id, out var user))
                {
                    list.Add(user);
                }
            }

            return list;
        }


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


        // 建完直接重拉: 面板发过来的那份是本地拼的, 没有服务端给的 id,
        // 而后续所有操作都要靠 id
        private async void OnDeptCreated(Department? dept)
        {
            await Reload();

            if (dept == null)
            {
                return;
            }

            // 按名字找回刚建的那个 -- 本地这份没有 id, 只能这样对
            var group = OrgPanel.Groups().FirstOrDefault(g => g.Dept?.Name == dept.Name);
            if (group?.GroupChatItem == null)
            {
                return;
            }

            group.Expand();
            OrgPanel.Select(group.GroupChatItem);
            OpenGroupChat(group);
        }


        private async void OnMemberCreated(User? user)
        {
            await Reload();

            if (user == null)
            {
                return;
            }

            var all = OrgPanel.AllMembers;
            all.Expand();

            var real = all.Members.FirstOrDefault(u => u.Username == user.Username);
            if (real == null)
            {
                return;
            }

            var row = all.RowOf(real);
            if (row != null)
            {
                OrgPanel.Select(row);
            }
            OpenMemberInfo(real);
        }


        private void OpenGroupChat(OrgGroup group)
        {
            current = group;
            ChatView.PeerName = group.Dept?.Name;
            ChatView.PeerStatus = $"{group.Members.Count} 名成员";
            ChatView.ClearMessages();
            ShowOnly(ChatView);
        }


        // 信息页当前显示的人, "消息"按钮点下来要知道找谁聊
        private User? memberShown;


        // 点了信息页的"消息": 切到聊天页并打开与他的会话
        private void OnMemberChat()
        {
            if (memberShown?.ID == null)
            {
                return;
            }

            if (TopLevel.GetTopLevel(this) is MainWindow main)
            {
                main.OpenChatTab();
            }

            BaseTab.Notify<ChatTab>(new Message(MsgID.OpenChat, memberShown, this));
        }


        // 点部门下的成员: 显示人员信息(与 ContactTab 的联系人信息同一控件)
        private void OpenMemberInfo(User user)
        {
            current = null;
            memberShown = user;

            var phone  = user.PhoneNum;
            var phones = string.IsNullOrWhiteSpace(phone) ? Array.Empty<string>() : new[] { phone };

            MemberView.SetContact(Avatars.Cached(user.Avatar) ?? Avatars.Default,
                                  user.Nickname ?? string.Empty,
                                  phones,
                                  user.Username ?? string.Empty);
            ShowOnly(MemberView);
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


        // 详情页改了名称/描述/头像: 左侧那一行和群聊顶栏都要跟着变
        private void OnDeptChanged(OrgGroup group)
        {
            group.Rebuild();

            if (ReferenceEquals(group, current))
            {
                ChatView.PeerName = group.Dept?.Name;
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
            MemberEditView.SetDepartments(OrgPanel.Departments());
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
            MemberView.IsVisible     = target == MemberView;
            OrgEditView.IsVisible    = target == OrgEditView;
            MemberEditView.IsVisible = target == MemberEditView;
            OrgDetailView.IsVisible  = target == OrgDetailView;
            SearchView.IsVisible     = target == SearchView;
            EmptyState.IsVisible     = target == EmptyState;
        }
    }
}
