using Avalonia.Controls;
using System;

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
            // 手机号是选填的, 没有就一个都不列 -- 总比拿别人的号码顶上去强
            var phone = item.User?.PhoneNum;
            var phones = string.IsNullOrWhiteSpace(phone) ? Array.Empty<string>() : new[] { phone };

            ContactView.SetContact(item.Avatar!, item.Nickname ?? "", phones, item.Sign ?? "");

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
