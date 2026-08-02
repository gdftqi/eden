using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Input;
using Avalonia.Interactivity;
using Lilith.Components;


namespace CC
{
    public partial class LoginWindow : Window
    {
        /// <param name="tip">回登录页的原因(如被顶号/被封禁), 空则不显示</param>
        public LoginWindow(string tip = "")
        {
            InitializeComponent();
            // 登录窗接管 Hydra 的高层回调, 用 no-op 顶掉可能残留的旧 MainWindow 处理器
            Hydra.Instance
                .SetOnStateChanged((_, _) => { })
                .SetOnPackage(_ => { });

            if (!string.IsNullOrEmpty(tip))
            {
                ShowError(tip);
            }
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

            // RA 登录 + KCP 握手全由 Hydra 完成(内部已捕获异常, 只返回是否连上)
            string err = await Hydra.Instance.Login(username, password);
            if (!string.IsNullOrEmpty(err))
            {
                SetLoading(false);
                ShowError(err);
                return;
            }

            // 连上 → 切主窗
            var main = new MainWindow();
            if (Application.Current?.ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
            {
                desktop.MainWindow = main;
            }
            main.Show();
            Close();
        }

        private void ShowError(string msg)
        {
            ErrorText.Text = msg;
            ErrorText.IsVisible = true;
        }

        // loading=true: 隐藏登录表单,显示转圈; false: 切回表单
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
