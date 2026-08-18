using SkiaSharp;
using System;
using System.IO;
using System.Runtime.InteropServices;


namespace CC.Utils
{
    public class Sys
    {
#if WINDOWS
        [DllImport("user32.dll", ExactSpelling = true)]
        private static extern IntPtr GetDesktopWindow();

        [DllImport("user32.dll", SetLastError = true, ExactSpelling = true)]
        private static extern IntPtr GetWindowDC(IntPtr hWnd);

        [DllImport("user32.dll", ExactSpelling = true)]
        private static extern int ReleaseDC(IntPtr hWnd, IntPtr hDC);

        [DllImport("gdi32.dll", SetLastError = true, ExactSpelling = true)]
        private static extern bool BitBlt(IntPtr hdcDest, int nXDest, int nYDest, int nWidth, int nHeight, IntPtr hdcSrc, int nXSrc, int nYSrc, int dwRop);

        [DllImport("gdi32.dll", SetLastError = true, ExactSpelling = true)]
        private static extern IntPtr CreateCompatibleDC(IntPtr hDC);

        [DllImport("gdi32.dll", SetLastError = true, ExactSpelling = true)]
        private static extern IntPtr CreateCompatibleBitmap(IntPtr hDC, int nWidth, int nHeight);

        [DllImport("gdi32.dll", ExactSpelling = true)]
        private static extern IntPtr SelectObject(IntPtr hDC, IntPtr hObject);

        [DllImport("gdi32.dll", ExactSpelling = true)]
        private static extern bool DeleteDC(IntPtr hdc);

        [DllImport("gdi32.dll", ExactSpelling = true)]
        private static extern bool DeleteObject(IntPtr hObject);

        [DllImport("gdi32.dll", SetLastError = true, ExactSpelling = true)]
        private static extern int GetDIBits(IntPtr hdc, IntPtr hbmp, uint uStartScan, uint cScanLines, [Out] byte[] lpvBits, ref BITMAPINFO lpbmi, uint uUsage);

        [StructLayout(LayoutKind.Sequential)]
        private struct BITMAPINFOHEADER
        {
            public uint biSize;
            public int biWidth;
            public int biHeight;
            public ushort biPlanes;
            public ushort biBitCount;
            public uint biCompression;
            public uint biSizeImage;
            public int biXPelsPerMeter;
            public int biYPelsPerMeter;
            public uint biClrUsed;
            public uint biClrImportant;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct BITMAPINFO
        {
            public BITMAPINFOHEADER bmiHeader;
            public uint mask0;
            public uint mask1;
            public uint mask2;
        }

        public static void CaptureDesktop(int width, int height)
        {
            if (!OperatingSystem.IsWindows())
                throw new PlatformNotSupportedException("桌面截图目前仅支持 Windows");

            const int SRCCOPY = 0x00CC0020;
            const int CAPTUREBLT = 0x40000000;   // 带上分层窗口

            IntPtr hDesktop = GetDesktopWindow();
            IntPtr hSrcDC = IntPtr.Zero, hDestDC = IntPtr.Zero;
            IntPtr hBitmap = IntPtr.Zero, hOldBmp = IntPtr.Zero;

            try
            {
                hSrcDC = GetWindowDC(hDesktop);
                if (hSrcDC == IntPtr.Zero)
                {
                    return;
                }

                hDestDC = CreateCompatibleDC(hSrcDC);
                hBitmap = CreateCompatibleBitmap(hSrcDC, width, height);
                if (hDestDC == IntPtr.Zero || hBitmap == IntPtr.Zero)
                {
                    return;
                }

                hOldBmp = SelectObject(hDestDC, hBitmap);

                if (!BitBlt(hDestDC, 0, 0, width, height, hSrcDC, 0, 0, SRCCOPY | CAPTUREBLT))
                {
                    return;
                }

                // 关键:取位之前先把 bitmap 从 DC 摘出来
                SelectObject(hDestDC, hOldBmp);
                hOldBmp = IntPtr.Zero;

                var bmi = new BITMAPINFO();
                bmi.bmiHeader.biSize = (uint)Marshal.SizeOf<BITMAPINFOHEADER>();
                bmi.bmiHeader.biWidth = width;
                bmi.bmiHeader.biHeight = -height;   // 负值 = 自上而下,和 Skia 一致
                bmi.bmiHeader.biPlanes = 1;
                bmi.bmiHeader.biBitCount = 32;
                bmi.bmiHeader.biCompression = 0;         // BI_RGB

                var rawPixels = new byte[width * height * 4];
                int scanned = GetDIBits(hDestDC, hBitmap, 0, (uint)height, rawPixels, ref bmi, 0);
                if (scanned != height)
                {
                    return;
                }

                var buf = EncodeBgraToPng(rawPixels, width, height);
                Save(buf, "1.png");
            }
            finally
            {
                if (hOldBmp != IntPtr.Zero)
                {
                    SelectObject(hDestDC, hOldBmp);
                }

                if (hBitmap != IntPtr.Zero)
                {
                    DeleteObject(hBitmap);
                }

                if (hDestDC != IntPtr.Zero)
                {
                    DeleteDC(hDestDC);
                }

                if (hSrcDC != IntPtr.Zero)
                {
                    ReleaseDC(hDesktop, hSrcDC);
                }
            }
        }
#elif MACOS
// TODO MacOS
#else
// TODO Linux
#endif
        public static byte[] EncodeBgraToPng(byte[] bgra, int width, int height, int quality = 80)
        {
            var info = new SKImageInfo(width, height, SKColorType.Bgra8888, SKAlphaType.Opaque);
            using var image = SKImage.FromPixelCopy(info, bgra);
            using var data = image.Encode(SKEncodedImageFormat.Png, quality);
            return data.ToArray();
        }

        public static void Save(byte[] buf, string path)
        {
            File.WriteAllBytes(path, buf);
        }
    }
}
