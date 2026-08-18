using Avalonia;
using Avalonia.Controls;
using Avalonia.Interactivity;
using CC.Eva;
using CC.Model;
using System;
using System.Collections.Generic;
using System.Threading.Tasks;

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
        }


        // 只拉一次: 四个页签都常驻在可视树里(靠 IsVisible 切), 进出不会重复触发
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
        /// 重新拉通讯录.
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
                Tips.Error($"加载联系人失败: {ex.Message}");
                return;
            }

            selectedItem = null;
            ContactList.Children.Clear();

            foreach (var u in rsp.Users ?? new List<User>())
            {
                AddContact(u);
            }
        }


        // 建一个联系人项
        private void AddContact(User user)
        {
            var cached = Avatars.Cached(user.Avatar);

            // 服务端没有"个性签名"这个字段, 副行放登录名 -- 重名时靠它区分
            var item = new ContactItem
            {
                User = user,
                Nickname = user.Nickname ?? string.Empty,
                Sign = user.Username ?? string.Empty,
                Avatar = cached ?? Avatars.Default,
            };

            // 缓存里没有就异步下, 回来再贴.item 只服务这一个人, 不存在贴错的问题
            if (cached == null && !string.IsNullOrEmpty(user.Avatar))
            {
                _ = Avatars.Load(user.Avatar).ContinueWith(t =>
                {
                    if (t.Result != null)
                    {
                        item.Avatar = t.Result;
                    }
                }, TaskScheduler.FromCurrentSynchronizationContext());
            }

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
