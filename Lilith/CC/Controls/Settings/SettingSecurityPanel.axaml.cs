using Avalonia.Controls;
using Avalonia.Interactivity;
using CC.Eva;
using Lilith.Utils;
using System;

namespace CC
{
    // 隐私安全: 改自己的密码.
    public partial class SettingSecurity : UserControl
    {
        // 改完密码要回登录页 -- 服务端会立刻作废会话, 留在主界面的话
        // 之后每个请求都会失败, 用户看不出为什么
        public event Action? PasswordChanged;

        public SettingSecurity()
        {
            InitializeComponent();
        }


        public void Reset()
        {
            OldBox.Text = string.Empty;
            NewBox.Text = string.Empty;
            ConfirmBox.Text = string.Empty;
        }


        private async void Save_Click(object? sender, RoutedEventArgs e)
        {
            var old = OldBox.Text ?? string.Empty;
            if (old.Length < 8 || old.Length > 16)
            {
                Tips.Error("请填写当前密码");
                return;
            }

            var pwd = NewBox.Text ?? string.Empty;
            if (pwd.Length < 8 || pwd.Length > 16)
            {
                Tips.Error("新密码需要 8 到 16 位");
                return;
            }

            if (pwd != (ConfirmBox.Text ?? string.Empty))
            {
                Tips.Error("两次输入的新密码不一致");
                return;
            }

            if (pwd == old)
            {
                Tips.Error("新密码不能与当前密码相同");
                return;
            }

            try
            {
                // 库里存的是 bcrypt(客户端 SHA256), 所以这里先哈希再发
                await UpdateUser.Password(Crypto.Sha256(old), Crypto.Sha256(pwd));
            }
            catch (Exception ex)
            {
                Tips.Error(ex.Message);
                return;
            }

            Reset();
            PasswordChanged?.Invoke();
        }
    }
}
