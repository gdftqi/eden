using System;
using System.IO;

namespace lilith.Tools
{
    // 简易文本日志: 排查断线重连问题用, 不依赖调试器附加(Debug.WriteLine 脱离调试器看不到)。
    // 每行立即 flush 到磁盘, 即便进程卡死/崩溃, 之前写的行也不会丢。
    public static class FileLog
    {
        private static readonly object sync = new object();
        private static readonly string path = Path.Combine(AppContext.BaseDirectory, "client.log");

        public static void Write(string msg)
        {
            var line = $"{DateTime.Now:HH:mm:ss.fff} [tid={System.Threading.Thread.CurrentThread.ManagedThreadId}] {msg}";
            lock (sync)
            {
                try
                {
                    File.AppendAllText(path, line + Environment.NewLine);
                }
                catch
                {
                    // 日志失败不能影响主流程
                }
            }
        }
    }
}
