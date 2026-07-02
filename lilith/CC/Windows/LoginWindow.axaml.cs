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

            SetLoading(true);   // 隐藏表单, 显示转圈

            var main = new MainWindow();

            try
            {
                await UserLogin.POST(username, password);

                var connectTask = KcpSession.Instance.Connect(main, Config.KCP_TIMEOUT);
                var done = await Task.WhenAny(connectTask, Task.Delay(8000));
                if (done != connectTask || !connectTask.Result)
                {
                    KcpSession.Instance.Close();   // 超时/握手失败 → 清理会话
                    SetLoading(false);
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
                SetLoading(false);
                ShowError("连接服务失败: " + ex.Message);
                return;
            }
        }

        private void ShowError(string msg)
        {
            ErrorText.Text = msg;
            ErrorText.IsVisible = true;
        }

        // loading=true: 隐藏登录表单、显示转圈; false: 切回表单
        private void SetLoading(bool loading)
        {
            LoginForm.IsVisible = !loading;
            LoadingPanel.IsVisible = loading;
            if (loading)
            {
                ErrorText.IsVisible = false;   // 进 loading 时清掉上次的报错
            }
        }
    }
}
