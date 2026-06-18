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

            BuildAvaloniaApp().StartWithClassicDesktopLifetime(args);
            mutex.ReleaseMutex();
        }

        public static AppBuilder BuildAvaloniaApp()
        {
            return AppBuilder.Configure<App>().UsePlatformDetect()
#if DEBUG
               .WithDeveloperTools()
#endif
               .WithInterFont().LogToTrace();
        }
    }
}
