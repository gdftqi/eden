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
            if (Application.Current?.ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop
                && desktop.MainWindow is MainWindow main)
            {
                main.ShowToast(text, ok);
            }
        }
    }
}
