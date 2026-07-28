using Avalonia;
using System;
using System.Threading;

namespace CC
{
    internal class Program
    {
        private static Mutex? mutex;

        [STAThread]
        public static void Main(string[] args)
        {
            bool createdNew;
            mutex = new Mutex(true, "CChatUniqueApplication", out createdNew);

            if (!createdNew)
            {
                return;
            }

            // Hydra 的配置 + pump 在 App.OnFrameworkInitializationCompleted(UI 线程就绪)里做
            BuildAvaloniaApp().StartWithClassicDesktopLifetime(args);
            mutex.ReleaseMutex();
        }

        public static AppBuilder BuildAvaloniaApp()
        {
            var builder = AppBuilder.Configure<App>().UsePlatformDetect();
#if DEBUG
            builder = builder.WithDeveloperTools();
#endif
            return builder.WithInterFont().LogToTrace();
        }
    }
}
