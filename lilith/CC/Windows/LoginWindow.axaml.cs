using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Input;
using Avalonia.Interactivity;
using lilith.Core;
using System;


namespace CC
{
    public partial class LoginWindow : Window
    {
        public LoginWindow()
        {
            InitializeComponent();
        }

        // 标题栏拖拽 (固定大小, 不做双击最大化)
        private void TopBar_PointerPressed(object? sender, PointerPressedEventArgs e)
        {
            if (e.GetCurrentPoint(this).Properties.IsLeftButtonPressed)
            {
                BeginMoveDrag(e);
            }
        }

        // 登录窗的 X = 退出程序
        private void Close_Click(object? sender, RoutedEventArgs e)
        {
            if (Application.Current?.ApplicationLifetime is IControlledApplicationLifetime life)
            {
                life.Shutdown();
            }
        }

        private void Login_Click(object? sender, RoutedEventArgs e)
        {
            var username = txtUsername.Text?.Trim() ?? "";
            var password = txtPassword.Text ?? "";

            if (username.Length < 6)
            {
                ShowError("请输入用户名");
                return;
            }

            if (password.Length < 8)
            {
                ShowError("请输入密码");
                return;
            }

            var main = new MainWindow();

            try
            {
                const string host = "13.214.204.197:5555";
                const uint conv = 2000;
                const string b64Token = "GCyA8dctBmgMpd66bczhC6Aqh0rtDu8gGwaPrzgrQjm7F50bBTyhsNYAOTHIoWQFUuNt7jq4sFcNJMjaBvYR5Ws46YMbsCNW07XgV+0JCx8Q9tgrJyO00Mssbt06+Pu/mOMClJmKpzt6sMjRVbZRPH5837tNBCaInd6Nr78FfyGs3JRP3/srinrujFOmOHZQaxCPoHmFz/zWIhl8CyFMqg==";
                const uint gwId = 1000;
                KcpSession.Instance.Connect(main, host, conv, b64Token, gwId);
                if (Application.Current?.ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
                {
                    desktop.MainWindow = main;
                }

                main.Show();
                Close();
            }
            catch (Exception ex)
            {
                ShowError("KCP连接失败: " + ex.Message);
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
