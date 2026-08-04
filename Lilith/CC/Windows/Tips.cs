using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Threading;

namespace CC
{
    public static class Tips
    {
        public static void Success(string text)
        {
            Show(text, true);
        }


        public static void Error(string text)
        {
            Show(text, false);
        }


        private static void Show(string text, bool ok)
        {
            if (Dispatcher.UIThread.CheckAccess())
            {
                Render(text, ok);
                return;
            }

            Dispatcher.UIThread.Post(() => Render(text, ok));
        }


        private static void Render(string text, bool ok)
        {
            // 提示条是主窗身上的部件.回登录页之后 desktop.MainWindow 已经换成 LoginWindow,
            // 这时无处可显示 -- 静默丢掉就行, 为了一条提示抛异常不值得
            if (Application.Current?.ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop
                && desktop.MainWindow is MainWindow main)
            {
                main.ShowToast(text, ok);
            }
        }
    }
}
