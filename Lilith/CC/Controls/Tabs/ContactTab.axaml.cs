using Avalonia.Controls;
using System;

namespace CC
{
    // 联系人页: ContactListPanel + 右侧详情(ContactWindow)
    public partial class ContactTab : BaseTab
    {
        // 详情页当前显示的人, "消息"按钮点下来要知道找谁聊
        private Model.User? contactShown;

        public ContactTab()
        {
            InitializeComponent();
            ContactPanel.ContactSelected += OpenContact;
            ContactPanel.AddContactRequested += OnAddContact;
            ContactView.CloseRequested += CloseContact;
            ContactView.ChatRequested += OnContactChat;
        }

        private void OpenContact(ContactItem item)
        {
            contactShown = item.User;

            // 手机号是选填的, 没有就一个都不列 -- 总比拿别人的号码顶上去强
            var phone = item.User?.PhoneNum;
            var phones = string.IsNullOrWhiteSpace(phone) ? Array.Empty<string>() : new[] { phone };

            ContactView.SetContact(item.Avatar!, item.Nickname ?? "", phones, item.Sign ?? "");

            EmptyState.IsVisible = false;
            ContactView.IsVisible = true;
        }

        // 点了详情页的"消息": 切到聊天页并打开与他的会话
        private void OnContactChat()
        {
            if (contactShown?.ID == null)
            {
                return;
            }

            if (TopLevel.GetTopLevel(this) is MainWindow main)
            {
                main.OpenChatTab();
            }

            BaseTab.Notify<ChatTab>(new Message(MsgID.OpenChat, contactShown, this));
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
