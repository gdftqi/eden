using Avalonia.Controls;

namespace CC
{
    // 联系人页: ContactListPanel + 右侧详情(ContactWindow)
    public partial class ContactTab : BaseTab
    {
        public ContactTab()
        {
            InitializeComponent();
            ContactPanel.ContactSelected += OpenContact;
            ContactPanel.AddContactRequested += OnAddContact;
            ContactView.CloseRequested += CloseContact;
        }

        private void OpenContact(ContactItem item)
        {
            // 临时: 手机号/状态用示例数据(以后按联系人查真实资料)
            ContactView.SetContact(item.Avatar!, item.Nickname ?? "",
                new[] { "+60 16-600 3526", "+86 138-0013-8000" },
                item.Sign ?? "");

            EmptyState.IsVisible = false;
            ContactView.IsVisible = true;
        }

        private void CloseContact()
        {
            ContactView.IsVisible = false;
            EmptyState.IsVisible = true;
        }

        private void OnAddContact()
        {
            // TODO: 添加联系人界面
        }
    }
}
