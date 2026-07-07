using Avalonia.Controls;

namespace CC
{
    // 联系人页: ContactListPanel + 右侧详情(后期填)
    public partial class ContactTab : UserControl
    {
        public ContactTab()
        {
            InitializeComponent();
            ContactPanel.ContactSelected += OpenContact;
            ContactPanel.AddContactRequested += OnAddContact;
        }

        private void OpenContact(string nick)
        {
            // TODO: 右侧显示联系人详情
        }

        private void OnAddContact()
        {
            // TODO: 添加联系人界面
        }
    }
}
