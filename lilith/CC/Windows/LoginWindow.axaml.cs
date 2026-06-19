using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Styling;
using lilith.Core;
using lilith.Utils;
using System;
using System.Diagnostics;

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
                KcpSession.Instance.Connect(main, 2000, "13.214.204.197:5555");
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
