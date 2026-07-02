using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Input;
using Avalonia.Interactivity;
using lilith.Core;
using System;
using lilith.Tools;
using System.Diagnostics;
using System.Threading.Tasks;
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
                await UserLogin.POST(username, password);

                // 等 KCP 握手结果; 8s 未连上按失败处理, 不切 MainWindow
                var connectTask = KcpSession.Instance.Connect(main, Config.KCP_TIMEOUT);
                var done = await Task.WhenAny(connectTask, Task.Delay(8000));
                if (done != connectTask || !connectTask.Result)
                {
                    KcpSession.Instance.Close();   // 超时/握手失败 → 清理会话
                    ShowError("连接网关失败, 请重试");
                    return;
                }

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
