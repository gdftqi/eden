using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Media;
using System;

namespace CC
{
    // 联系人列表面板: 标题 + 搜索栏 + 添加联系人 + 列表; 选中联系人抛 ContactSelected 给宿主
    public partial class ContactListPanel : UserControl
    {
        // 选中某个联系人时触发(宿主据此打开右侧详情)
        public event Action<ContactItem>? ContactSelected;

        // 点了"添加联系人"时触发(宿主决定弹窗还是切界面)
        public event Action? AddContactRequested;

        // 当前选中的联系人项(高亮由 IsSelected 驱动, 不依赖焦点)
        private ContactItem? selectedItem;

        public ContactListPanel()
        {
            InitializeComponent();
            LoadSampleContacts();
        }

        // 数据来自 Model.ContactSource -- 与"创建部门-添加人员"共用同一份
        private void LoadSampleContacts()
        {
            foreach (var c in Model.ContactSource.All)
            {
                AddContact(Model.ContactSource.Avatar, c.Nickname, c.Sign);
            }
        }

        // 建一个联系人项
        private void AddContact(IImage avatar, string nick, string sign)
        {
            var item = new ContactItem { Avatar = avatar, Nickname = nick, Sign = sign };
            item.PointerPressed += (_, _) => Select(item);
            ContactList.Children.Add(item);
        }

        // 切换选中项: 旧的熄灭, 新的点亮, 再通知宿主
        private void Select(ContactItem item)
        {
            if (selectedItem != null)
            {
                selectedItem.IsSelected = false;
            }
            selectedItem = item;
            item.IsSelected = true;
            ContactSelected?.Invoke(item);
        }

        private void AddContact_Click(object? sender, RoutedEventArgs e)
        {
            AddContactRequested?.Invoke();
        }

        private void ClearSearch_Click(object? sender, RoutedEventArgs e)
        {// 搜索框 清空文字
            SearchBox.Text = string.Empty;
            SearchBox.Focus();
        }
    }
}
