using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Input;
using Avalonia.Interactivity;
using lilith.Core;

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
            var user = UserBox.Text?.Trim() ?? "";
            var pass = PassBox.Text ?? "";

            if (user.Length == 0 || pass.Length == 0)
            {
                ShowError("请输入用户名和密码");
                return;
            }

            //KcpSession.Instance.Connect(1, "13.214.204.197:5555");

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
    }
}
