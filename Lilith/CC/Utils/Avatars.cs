using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using System;
using System.Collections.Generic;
using System.IO;
using Lilith.Utils;
using System.Net.Http;
using System.Threading.Tasks;

namespace CC
{
    /// <summary>
    /// 头像缓存.
    /// </summary>
    public static class Avatars
    {
        private static readonly HttpClient hc = new() { Timeout = TimeSpan.FromSeconds(15) };

        // url -> 已解码的图
        private static readonly Dictionary<string, Bitmap> cache = new();

        // url -> 正在下的那次. 同一张图被多行同时请求时只下一遍
        private static readonly Dictionary<string, Task<Bitmap?>> loading = new();


        private static IImage? def;

        /// <summary>
        /// 默认头像
        /// </summary>
        public static IImage Default
        {
            get
            {
                if (def != null)
                {
                    return def;
                }

                var person = Geometry.Parse("M12 12c2.21 0 4-1.79 4-4s-1.79-4-4-4-4 1.79-4 4 1.79 4 4 4zm0 2c-2.67 0-8 1.34-8 4v2h16v-2c0-2.66-5.33-4-8-4z");
                person.Transform = new MatrixTransform(
                    Matrix.CreateScale(0.66, 0.66) * Matrix.CreateTranslation(4.08, 4.08));

                var group = new DrawingGroup();
                group.Children.Add(new GeometryDrawing
                {
                    Brush = new SolidColorBrush(Color.Parse("#E4E7EB")),
                    Geometry = new EllipseGeometry(new Rect(0, 0, 24, 24)),
                });
                group.Children.Add(new GeometryDrawing
                {
                    Brush = new SolidColorBrush(Color.Parse("#9AA0A6")),
                    Geometry = person,
                });

                def = new DrawingImage { Drawing = group };
                return def;
            }
        }


        /// <summary>
        /// 把头像挂到一个 Image
        /// </summary>
        public static void Bind(Image target, string? url)
        {
            target.Tag = url;

            var bmp = Cached(url);
            target.Source = bmp ?? Default;

            if (bmp != null || string.IsNullOrEmpty(url))
            {
                return;
            }

            var want = url;
            _ = Load(url).ContinueWith(t =>
            {
                if (t.Result != null && (target.Tag as string) == want)
                {
                    target.Source = t.Result;
                }
            }, TaskScheduler.FromCurrentSynchronizationContext());
        }


        /// <summary>
        /// 已经缓存的话直接给, 没有则返回 null.
        /// </summary>
        public static Bitmap? Cached(string? url)
        {
            if (string.IsNullOrEmpty(url))
            {
                return null;
            }

            lock (cache)
            {
                return cache.TryGetValue(url, out var bmp) ? bmp : null;
            }
        }


        /// <summary>
        /// 直接塞一张进缓存. 刚上传完的图已经在手里, 不必等下次显示时重新下.
        /// </summary>
        public static void Put(string? url, Bitmap bmp)
        {
            if (string.IsNullOrEmpty(url))
            {
                return;
            }

            lock (cache)
            {
                cache[url] = bmp;
            }
        }


        /// <summary>
        /// 取头像, 没缓存就下载. 失败返回 null
        /// </summary>
        public static Task<Bitmap?> Load(string? url)
        {
            if (string.IsNullOrEmpty(url))
            {
                return Task.FromResult<Bitmap?>(null);
            }

            lock (cache)
            {
                if (cache.TryGetValue(url, out var bmp))
                {
                    return Task.FromResult<Bitmap?>(bmp);
                }

                if (loading.TryGetValue(url, out var pending))
                {
                    return pending;
                }

                var task = Download(url);
                loading[url] = task;
                return task;
            }
        }


        private static async Task<Bitmap?> Download(string url)
        {
            Bitmap? bmp = null;

            try
            {
                var bytes = await hc.GetByteArrayAsync(url);
                using var ms = new MemoryStream(bytes);
                bmp = new Bitmap(ms);
            }
            catch (Exception ex)
            {
                Log.Write($"[Avatars] 下载失败: {url} | {ex.Message}");
            }

            lock (cache)
            {
                loading.Remove(url);
                if (bmp != null)
                {
                    cache[url] = bmp;
                }
            }

            return bmp;
        }
    }
}
