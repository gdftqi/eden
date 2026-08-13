using System;
using System.Collections.Generic;

namespace CC.Model
{
    /// <summary>
    /// 会话(本地实体), UI 的 ChatItem 控件绑定它.
    /// </summary>
    public class ChatConversation
    {
        // 建表语句(幂等, 每次启动执行). 改列时同步维护顶部注释和 Eva/sqlite/00_init_sqlite.sql
        public const string DDL = @"
CREATE TABLE IF NOT EXISTS t_chat_conversation (
    f_chat_id       INTEGER PRIMARY KEY,
    f_peer_id       INTEGER NOT NULL,
    f_recv_seq      INTEGER NOT NULL DEFAULT 0,
    f_read_seq      INTEGER NOT NULL DEFAULT 0,
    f_peer_read_seq INTEGER NOT NULL DEFAULT 0,
    f_unread        INTEGER NOT NULL DEFAULT 0,
    f_last_preview  TEXT,
    f_last_time     INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX IF NOT EXISTS idx_conv_sort ON t_chat_conversation(f_last_time DESC);
";

        /// <summary>
        /// 会话 ID
        /// </summary>
        public Int64 ChatId { get; set; }

        /// <summary>
        /// 单聊对端 uid.
        /// </summary>
        public Int64 PeerId { get; set; }

        /// <summary>
        /// 本地已收到的最大 seq, 重连时上报.
        /// </summary>
        public Int64 RecvSeq { get; set; }

        /// <summary>
        /// 本端已读位置.
        /// </summary>
        public Int64 ReadSeq { get; set; }

        /// <summary>
        /// 对方已读到哪, 双勾蓝.
        /// </summary>
        public Int64 PeerReadSeq { get; set; }

        /// <summary>
        /// 未读数(冗余计数, 列表页不重新 COUNT).
        /// </summary>
        public int Unread { get; set; }

        /// <summary>
        /// 最后一条消息摘要.
        /// </summary>
        public string LastPreview { get; set; } = string.Empty;

        /// <summary>
        /// 最后一条消息时间(毫秒).
        /// </summary>
        public Int64 LastTime { get; set; }

        // ---- 展示缓存(来自组织架构/资料, 不落表) ----

        public string PeerName   { get; set; } = string.Empty;
        public string PeerAvatar { get; set; } = string.Empty;

        /// <summary>
        /// 消息列表
        /// </summary>
        public List<ChatMessage> Messages { get; } = new();


        /// <summary>
        /// 计算 ChatID
        /// </summary>
        public static Int64 MakeChatId(Int64 a, Int64 b)
        {
            Int64 lo = Math.Min(a, b);
            Int64 hi = Math.Max(a, b);
            return lo << 32 | hi;
        }
    }
}
