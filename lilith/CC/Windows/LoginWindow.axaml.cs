using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Input;
using Avalonia.Interactivity;
using lilith.Core;
using System;
using lilith.Tools;
using System.Diagnostics;
using CC.Proxy;


namespace CC
{
    public partial class LoginWindow : Window
    {
        public LoginWindow()
        {
            InitializeComponent();
        }

        private void TopBar_PointerPressed(object? sender, PointerPressedEventArgs e)
        {// TopBar 拖动窗体
            if (e.GetCurrentPoint(this).Properties.IsLeftButtonPressed)
            {
                BeginMoveDrag(e);
            }
        }

        private void Close_Click(object? sender, RoutedEventArgs e)
        {// 退出程序
            if (Application.Current?.ApplicationLifetime is IControlledApplicationLifetime life)
            {
                life.Shutdown();
            }
        }

        private async void Login_Click(object? sender, RoutedEventArgs e)
        {// 登录
            var username = txtUsername.Text?.Trim() ?? "";
            var password = txtPassword.Text ?? "";

            if (username.Length < 6)
            {
                ShowError("无效的用户名");
                return;
            }

            if (password.Length < 8)
            {
                ShowError("无效的密码");
                return;
            }

            var main = new MainWindow();

            try
            {
                int res = await UserLogin.POST(username, password);

                const string host = "13.214.204.197:5555";
                const uint conv = 2000;
                const uint userId = 90000;
                // test_kcp.py 同款: conv=2000, user_id=90000 (164B sealed token, 网关 x25519 公钥加密)
                const string b64Token = "q2NxfAgZABiSo0494vmkCXfLTv50Dh0viulcssIfPjmLIg/OD4VHiNsu9Lcf267mjsXtT7E6C6WExNfWsZF/dYZSLve8ApDdfQTUCH8jhpYXM8tJwQjWbjLkLitXETsuR3kU7QHJfEU5HkOctp6gBcHvncFmER4JS82kWUCE71M1i3/MWPMa7+1UqRhe2hvnLZMhQgIs7IpHnkopFDBibQSsuBo=";
                const uint gwId = 1000;
                KcpSession.Instance.Connect(main, host, conv, userId, b64Token, gwId);
                if (Application.Current?.ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
                {
                    desktop.MainWindow = main;
                }

                main.Show();
                Close();
            }
            catch (Exception ex)
            {
                Debug.WriteLine(ex.Message);
                ShowError("连接服务失败: " + ex.Message);
                return;
            }
        }

        private void ShowError(string msg)
        {
            ErrorText.Text = msg;
            ErrorText.IsVisible = true;
        }
    }
}
