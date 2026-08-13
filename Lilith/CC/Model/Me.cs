using Lilith.Core;
using Lilith.Utils;
using Microsoft.Data.Sqlite;
using System;
using System.IO;

namespace CC.Model
{
    /// <summary>
    /// 当前登录的用户
    /// </summary>
    public static class Me
    {
        public static User? User { get; private set; }

        /// <summary>
        /// 当前账号的本地库(im_&lt;uid&gt;.db). 登录成功后可用; 只在 UI 线程用.
        /// </summary>
        public static SqliteConnection? Db { get; private set; }


        /// <summary>
        /// 当前用户 id.没登录时是 0.
        /// </summary>
        public static Int64 ID
        {
            get { return User?.ID ?? HttpSession.Instance.UserID ?? 0; }
        }


        /// <summary>
        /// 从登录应答里取出当前用户.登录成功后调一次.
        /// </summary>
        public static void Sync()
        {
            User = HttpSession.Instance.User?.ToObject<User>();
            OpenDb();
        }


        public static void Set(User user)
        {
            User = user;
        }


        // 打开(必要时创建)本账号的库: 建表 + 重放连接级 PRAGMA.
        // 每账号一个文件, 切号就是换文件, 表里不需要 owner 列
        private static void OpenDb()
        {
            Db?.Dispose();
            Db = null;

            if (ID == 0)
            {
                return;
            }

            try
            {
                var dir = Path.Combine(
                    Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "CC");
                Directory.CreateDirectory(dir);

                var db = new SqliteConnection($"Data Source={Path.Combine(dir, $"im_{ID}.db")}");
                db.Open();

                using var cmd = db.CreateCommand();
                // 前几条 PRAGMA 是连接级的, 不写进文件, 每次 open 都必须重放
                cmd.CommandText = @"
PRAGMA journal_mode = WAL;
PRAGMA synchronous  = NORMAL;
PRAGMA foreign_keys = ON;
PRAGMA temp_store   = MEMORY;
PRAGMA mmap_size    = 268435456;
PRAGMA cache_size   = -8000;
PRAGMA user_version = 1;
" + ChatConversation.DDL + ChatMessage.DDL;
                cmd.ExecuteNonQuery();

                Db = db;
            }
            catch (Exception ex)
            {
                // 库开不了不挡登录: 聊天先降级为纯内存, 别的功能照常
                Log.Write($"[Me] 本地库打开失败: {ex.Message}");
            }
        }
    }
}
