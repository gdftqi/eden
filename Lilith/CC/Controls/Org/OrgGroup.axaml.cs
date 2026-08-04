using Avalonia.Controls;
using Avalonia.Media.Transformation;
using CC.Model;
using System;
using System.Collections.Generic;

namespace CC
{
    public partial class OrgGroup : UserControl
    {
        public event Action<OrgGroup>? GroupChatSelected;
        public event Action<OrgItem, User>? MemberSelected;

        public Department? Dept { get; private set; }
        public IList<User> Members { get; } = new List<User>();

        public bool HasGroupChat { get; private set; } = true;

        // 群聊那一行.选中态/未读都挂在它身上
        public OrgItem? GroupChatItem { get; private set; }

        private bool expanded;

        public OrgGroup()
        {
            InitializeComponent();
            Header.PointerPressed += (_, _) => Toggle();
        }


        public void Bind(Department dept, IEnumerable<User>? members = null, bool hasGroupChat = true)
        {
            Dept = dept;
            HasGroupChat = hasGroupChat;

            Members.Clear();
            if (members != null)
            {
                foreach (var u in members)
                {
                    Members.Add(u);
                }
            }

            Rebuild();
        }


        public void Toggle()
        {
            expanded = !expanded;
            Body.IsVisible = expanded;
            Arrow.RenderTransform = TransformOperations.Parse(expanded ? "rotate(90deg)" : "rotate(0deg)");
        }


        public void Expand()
        {
            if (!expanded)
            {
                Toggle();
            }
        }


        public void Rebuild()
        {
            TitleText.Text = Dept?.Name ?? string.Empty;
            CountText.Text = Members.Count > 0 ? Members.Count.ToString() : string.Empty;

            Body.Children.Clear();
            GroupChatItem = null;

            if (HasGroupChat && Dept != null)
            {
                var chat = new OrgItem { Dept = Dept };
                chat.PointerPressed += (_, _) => GroupChatSelected?.Invoke(this);
                GroupChatItem = chat;
                Body.Children.Add(chat);
            }

            foreach (var u in Members)
            {
                var row = new OrgItem { User = u };
                var user = u;
                row.PointerPressed += (_, _) => MemberSelected?.Invoke(row, user);
                Body.Children.Add(row);
            }
        }


        // 某个成员对应的那一行, Rebuild 之后才有
        public OrgItem? RowOf(User user)
        {
            foreach (var row in Rows())
            {
                if (ReferenceEquals(row.User, user))
                {
                    return row;
                }
            }
            return null;
        }


        public IEnumerable<OrgItem> Rows()
        {
            foreach (var c in Body.Children)
            {
                if (c is OrgItem item)
                {
                    yield return item;
                }
            }
        }
    }
}
