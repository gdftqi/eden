using Avalonia;
using lilith.Tools;
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

            HttpSession.Instance.Init(Config.HTTP_HOST, Config.HTTP_X25519_PK);
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
