using System;

namespace CC.Model
{
    /// <summary>
    /// 聊天消息(本地实体)
    ///
    /// SQLite t_chat_message
    /// </summary>
    public class ChatMessage
    {
        // 建表语句(幂等, 每次启动执行). 改列时同步维护 Eva/sqlite/00_init_sqlite.sql
        public const string DDL = @"
CREATE TABLE IF NOT EXISTS t_chat_message (
    f_local_id   INTEGER PRIMARY KEY AUTOINCREMENT,
    f_chat_id    INTEGER NOT NULL,
    f_cli_id     INTEGER NOT NULL,
    f_seq        INTEGER NOT NULL DEFAULT 0,
    f_msg_id     INTEGER NOT NULL DEFAULT 0,
    f_from_id    INTEGER NOT NULL,
    f_to_id      INTEGER NOT NULL,
    f_msg_type   INTEGER NOT NULL,
    f_content    TEXT,
    f_status     INTEGER NOT NULL DEFAULT 0,
    f_edit_seq   INTEGER NOT NULL DEFAULT 0,
    f_edited_at  INTEGER,
    f_is_revoked INTEGER NOT NULL DEFAULT 0,
    f_created_at INTEGER NOT NULL,
    f_is_deleted INTEGER NOT NULL DEFAULT 0
);
CREATE UNIQUE INDEX IF NOT EXISTS idx_msg_client ON t_chat_message(f_chat_id, f_from_id, f_cli_id);
CREATE UNIQUE INDEX IF NOT EXISTS idx_msg_seq    ON t_chat_message(f_chat_id, f_seq) WHERE f_seq > 0;
CREATE INDEX        IF NOT EXISTS idx_msg_id     ON t_chat_message(f_msg_id) WHERE f_msg_id > 0;
";

        /// <summary>
        /// 幂等 ID(本地自增). 发送带上它, ACK 回来靠它认领本条;
        /// 收到的消息填对方 NTF 里的 cli_id.
        /// </summary>
        public Int64 ClientId { get; set; }

        /// <summary>
        /// 会话内序号(服务端分配). 0 = 还没 ACK.
        /// </summary>
        public Int64 Seq { get; set; }

        /// <summary>
        /// 全局消息 ID(雪花). 0 = 还没 ACK. 撤回/编辑的目标用它.
        /// </summary>
        public Int64 MsgId { get; set; }

        /// <summary>
        /// 发送者 uid.
        /// </summary>
        public Int64 FromId { get; set; }

        /// <summary>
        /// 消息类型, 数值与 ccs.proto 的 MessageType 一致(1文本 2图片...).
        /// </summary>
        public int Type { get; set; }

        /// <summary>
        /// 文本为原文; 富媒体为 JSON(存对象 key).
        /// </summary>
        public string Content { get; set; } = string.Empty;

        /// <summary>
        /// 0 发送中, 1 成功, 2 失败.
        /// </summary>
        public int Status { get; set; }

        /// <summary>
        /// 服务端毫秒时间戳. 未 ACK 前用本机时间占位, ACK 后以服务端为准.
        /// </summary>
        public Int64 CreatedAt { get; set; }

        /// <summary>
        /// 已应用的最后一次编辑事件 seq, 兼作版本号.
        /// </summary>
        public Int64 EditSeq { get; set; }

        /// <summary>
        /// 撤回标记. 置位后 Content 清空, 渲染成占位提示.
        /// </summary>
        public bool IsRevoked { get; set; }

        /// <summary>
        /// 是不是自己发的(气泡靠右/靠左).
        /// </summary>
        public bool Mine
        {
            get { return FromId == Me.ID; }
        }
    }
}
