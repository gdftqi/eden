using Avalonia.Platform;
using Lilith.Utils;
using System;
using System.IO;
using System.Runtime.InteropServices;

namespace CC.Utils
{
    /// <summary>
    /// 提示音. 声音出不来绝不能影响收消息, 所以这里所有异常都吞掉只记日志.
    /// </summary>
    public static class Sound
    {
#if WINDOWS
        [DllImport("winmm.dll", SetLastError = true)]
        private static extern bool PlaySound(IntPtr data, IntPtr hMod, uint flags);

        private const uint SND_ASYNC     = 0x0001;   // 立即返回, 不等播完
        private const uint SND_NODEFAULT = 0x0002;   // 放不出来就安静, 别响系统默认音
        private const uint SND_MEMORY    = 0x0004;   // data 指向内存里的 wav 而不是文件名
#endif

        // 两次提示音的最小间隔: 对方连发时不至于响成一片
        private const int MIN_INTERVAL_MS = 500;

        private static IntPtr wav = IntPtr.Zero;
        private static bool   failed;
        private static long   lastTick;

        /// <summary>
        /// 收到新消息的提示音.
        /// </summary>
        public static void Notify()
        {
#if WINDOWS
            long now = Environment.TickCount64;
            if (now - lastTick < MIN_INTERVAL_MS)
            {
                return;
            }
            lastTick = now;

            if (!Load())
            {
                return;
            }

            PlaySound(wav, IntPtr.Zero, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
#endif
        }


#if WINDOWS
        private static bool Load()
        {
            if (wav != IntPtr.Zero)
            {
                return true;
            }

            if (failed)
            {
                return false;   // 读失败过一次就别反复试了
            }

            try
            {
                using var src = AssetLoader.Open(new Uri("avares://CC/Resources/notify.wav"));
                using var ms = new MemoryStream();
                src.CopyTo(ms);

                var bytes = ms.ToArray();
                var buf = Marshal.AllocHGlobal(bytes.Length);
                Marshal.Copy(bytes, 0, buf, bytes.Length);
                wav = buf;

                return true;
            }
            catch (Exception ex)
            {
                failed = true;
                Log.Write($"[Sound] 提示音加载失败: {ex.Message}");
                return false;
            }
        }
#endif
    }
}
