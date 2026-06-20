using Avalonia;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;

namespace CC.Utils
{
    public class Sys
    {
        [DllImport("user32.dll")] private static extern IntPtr GetDesktopWindow();
        [DllImport("user32.dll")] private static extern IntPtr GetWindowDC(IntPtr hWnd);
        [DllImport("user32.dll")] private static extern int ReleaseDC(IntPtr hWnd, IntPtr hDC);
        [DllImport("gdi32.dll")] private static extern bool BitBlt(IntPtr hdcDest, int nXDest, int nYDest, int nWidth, int nHeight, IntPtr hdcSrc, int nXSrc, int nYSrc, int dwRop);
        [DllImport("gdi32.dll")] private static extern IntPtr CreateCompatibleDC(IntPtr hDC);
        [DllImport("gdi32.dll")] private static extern IntPtr CreateCompatibleBitmap(IntPtr hDC, int nWidth, int nHeight);
        [DllImport("gdi32.dll")] private static extern IntPtr SelectObject(IntPtr hDC, IntPtr hObject);
        [DllImport("gdi32.dll")] private static extern bool DeleteDC(IntPtr hdc);
        [DllImport("gdi32.dll")] private static extern bool DeleteObject(IntPtr hObject);
        [DllImport("gdi32.dll")] private static extern bool GetDIBits(IntPtr hdc, IntPtr hbmp, uint uStartScan, uint cScanLines, [Out] byte[] lpvBits, ref BITMAPINFO lpbmi, uint uUsage);

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
            [MarshalAs(UnmanagedType.ByValArray, SizeConst = 4)]
            public byte[] bmiColors;
        }

        static public unsafe void CaptureWindowsDesktopNative(int width, int height)
        {
            IntPtr hSrcDC = GetWindowDC(GetDesktopWindow());
            IntPtr hDestDC = CreateCompatibleDC(hSrcDC);
            IntPtr hBitmap = CreateCompatibleBitmap(hSrcDC, width, height);
            IntPtr hOldBmp = SelectObject(hDestDC, hBitmap);

            // 屏幕拷贝 (SRCCOPY)
            BitBlt(hDestDC, 0, 0, width, height, hSrcDC, 0, 0, 0x00CC0020);

            BITMAPINFO bmi = new BITMAPINFO();
            bmi.bmiHeader.biSize = (uint)Marshal.SizeOf(typeof(BITMAPINFOHEADER));
            bmi.bmiHeader.biWidth = width;
            bmi.bmiHeader.biHeight = -height;
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;

            byte[] rawPixels = new byte[width * height * 4];
            GetDIBits(hDestDC, hBitmap, 0, (uint)height, rawPixels, ref bmi, 0);

            SelectObject(hDestDC, hOldBmp);
            DeleteObject(hBitmap);
            DeleteDC(hDestDC);
            ReleaseDC(GetDesktopWindow(), hSrcDC);

            var pixelSize = new PixelSize(width, height);
            var dpi = new Vector(96, 96);
            var avaloniaBitmap = new WriteableBitmap(pixelSize, dpi, PixelFormat.Bgra8888, AlphaFormat.Premul);

            using (var lockedBuffer = avaloniaBitmap.Lock())
            {
                Marshal.Copy(rawPixels, 0, lockedBuffer.Address, rawPixels.Length);
            }

            string savePath = Path.Combine(AppContext.BaseDirectory, "desktop_native.png");
            avaloniaBitmap.Save(savePath);
        }
    }
}
