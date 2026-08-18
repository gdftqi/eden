-- ============================================================
-- 连接参数
-- ============================================================
-- 每个账号一个独立 db 文件:im_<uid>.db,多账号切换直接换文件

PRAGMA journal_mode = WAL;        -- 读写不互斥,收消息不阻塞列表渲染
PRAGMA user_version = 1;          -- schema 版本,启动时比对决定跑哪些迁移脚本
PRAGMA synchronous  = NORMAL;     -- WAL 下足够安全;FULL 每条消息多一次 fsync
PRAGMA foreign_keys = ON;         -- 目前没有任何 FOREIGN KEY 约束, 空设; 留着以防后面加
PRAGMA temp_store   = MEMORY;
PRAGMA mmap_size    = 268435456;  -- 256MB
PRAGMA cache_size   = -8000;      -- 8MB


-- ============================================================
-- 会话列表
-- ============================================================
CREATE TABLE t_chat_cursor (
    f_chat_id       INTEGER PRIMARY KEY,
    f_peer_id       INTEGER NOT NULL,
    f_recv_seq      INTEGER NOT NULL DEFAULT 0,   -- 本地已收到的最大 seq, 重连时上报
    f_read_seq      INTEGER NOT NULL DEFAULT 0,   -- 本端已读位置
    f_peer_read_seq INTEGER NOT NULL DEFAULT 0,   -- 对方已读到哪, 双勾蓝
    f_unread        INTEGER NOT NULL DEFAULT 0,   -- 冗余计数, 列表页不能 COUNT
    f_last_preview  TEXT,
    f_last_time     INTEGER NOT NULL DEFAULT 0    -- 毫秒
);
CREATE INDEX idx_cursor_sort ON t_chat_cursor(f_last_time DESC);


-- ============================================================
-- 消息
-- ============================================================
CREATE TABLE t_chat_message (
    f_local_id   INTEGER PRIMARY KEY AUTOINCREMENT,
    f_chat_id    INTEGER NOT NULL,
    f_cli_id     INTEGER NOT NULL,            -- 幂等号: 自己发的 = f_local_id(插入后回填)
    f_seq        INTEGER NOT NULL DEFAULT 0,  -- 0 = 未确认
    f_msg_id     INTEGER NOT NULL DEFAULT 0,  -- 0 = 未确认
    f_from_id    INTEGER NOT NULL,
    f_to_id      INTEGER NOT NULL,
    f_msg_type   INTEGER NOT NULL,            -- 数值同 ccs.proto MessageType
    f_content    TEXT,
    f_status     INTEGER NOT NULL DEFAULT 0,  -- 0 发送中 1 成功 2 失败
    f_edit_seq   INTEGER NOT NULL DEFAULT 0,  -- 已应用的最后一次编辑事件 seq, 防乱序回退
    f_edited_at  INTEGER,                     -- 有值即显示"已编辑"
    f_is_revoked INTEGER NOT NULL DEFAULT 0,
    f_created_at INTEGER NOT NULL,            -- 毫秒, 以服务端为准
    f_is_deleted INTEGER NOT NULL DEFAULT 0   -- 本端删除(仅自己不可见)
);

CREATE UNIQUE INDEX idx_msg_client ON t_chat_message(f_chat_id, f_from_id, f_cli_id);
-- ↑ 幂等基石: 重发/重连/重复推送靠 INSERT OR IGNORE 天然去重; 带 from 因为各发送方各自递增
CREATE UNIQUE INDEX idx_msg_seq ON t_chat_message(f_chat_id, f_seq) WHERE f_seq > 0;
-- ↑ 部分索引: 未确认的都是 0, 不排除会互撞
CREATE INDEX idx_msg_id   ON t_chat_message(f_msg_id) WHERE f_msg_id > 0;
-- ↑ 编辑/撤回事件只带 msg_id, 靠它定位目标行


-- ============================================================
-- 待发队列
-- ============================================================
-- 发送/编辑/撤回等待确认的操作. App 被杀后重启要能继续重试.
-- 不放消息表里: 编辑和撤回没有对应的消息行
CREATE TABLE t_chat_outbox (
    f_id         INTEGER PRIMARY KEY AUTOINCREMENT,
    f_chat_id    INTEGER NOT NULL,
    f_op_type    INTEGER NOT NULL,     -- 1 发送 2 编辑 3 撤回 4 已读上报
    f_cli_id     INTEGER NOT NULL,     -- 幂等号(= t_chat_message.f_local_id), 重试不变
    f_target_id  INTEGER,              -- 编辑/撤回的目标 msg_id
    f_payload    TEXT,                 -- 新内容等参数
    f_prev_value TEXT,                 -- 操作前旧值, 失败回滚本地乐观更新
    f_retry_cnt  INTEGER NOT NULL DEFAULT 0,
    f_created_at INTEGER NOT NULL
);
CREATE INDEX idx_outbox_chat ON t_chat_outbox(f_chat_id, f_id);


-- ============================================================
-- 空洞记录
-- ============================================================
-- 离线期间漏收的区间 [f_start_seq, f_end_seq]. 翻页时据此判断读本地还是拉服务端.
-- seq 本身允许有空洞(服务端号段浪费), 区间是否补齐以服务端应答为准, 不能靠连续性推断
CREATE TABLE t_chat_gap (
    f_chat_id   INTEGER NOT NULL,
    f_start_seq INTEGER NOT NULL,
    f_end_seq   INTEGER NOT NULL,
    PRIMARY KEY (f_chat_id, f_start_seq)
);


-- ============================================================
-- 资料缓存
-- ============================================================
CREATE TABLE t_chat_user_cache (
    f_user_id  INTEGER PRIMARY KEY,
    f_nickname TEXT,
    f_avatar   TEXT,
    f_remark   TEXT,                         -- 本地备注, 显示优先于 nickname
    f_version  INTEGER NOT NULL DEFAULT 0    -- 服务端资料版本, 增量拉取用
);

CREATE TABLE t_chat_group_member (
    f_chat_id INTEGER NOT NULL,
    f_user_id INTEGER NOT NULL,
    f_role    INTEGER,            -- 1 普通 2 管理员 3 群主
    f_alias   TEXT,               -- 群昵称
    PRIMARY KEY (f_chat_id, f_user_id)
);


-- ============================================================
-- 杂项
-- ============================================================
CREATE TABLE t_chat_kv (
    f_key TEXT PRIMARY KEY,
    f_val TEXT
);   -- 全局同步位点、配置、登录态等


-- ============================================================
-- 全文搜索
-- ============================================================
CREATE VIRTUAL TABLE t_chat_message_fts USING fts5(
    f_content,
    content='t_chat_message',     -- 外部内容表, 不重复存正文
    content_rowid='f_local_id',
    tokenize='trigram'            -- 中文必须用 trigram, unicode61 整句成一词搜不出
);

CREATE TRIGGER trg_fts_ins AFTER INSERT ON t_chat_message
WHEN new.f_msg_type = 1 BEGIN     -- 只索引文本
    INSERT INTO t_chat_message_fts(rowid, f_content) VALUES (new.f_local_id, new.f_content);
END;

CREATE TRIGGER trg_fts_upd AFTER UPDATE OF f_content ON t_chat_message
WHEN new.f_msg_type = 1 AND new.f_is_deleted = 0 BEGIN
                                  -- 编辑后重建索引, 否则搜到旧文本
    INSERT INTO t_chat_message_fts(t_chat_message_fts, rowid, f_content)
    VALUES ('delete', old.f_local_id, old.f_content);
    INSERT INTO t_chat_message_fts(rowid, f_content) VALUES (new.f_local_id, new.f_content);
END;

-- 本端删除只打标不触发 AFTER DELETE, 单独摘索引, 否则搜得出已删消息
CREATE TRIGGER trg_fts_del_flag AFTER UPDATE OF f_is_deleted ON t_chat_message
WHEN new.f_msg_type = 1 AND new.f_is_deleted = 1 AND old.f_is_deleted = 0 BEGIN
    INSERT INTO t_chat_message_fts(t_chat_message_fts, rowid, f_content)
    VALUES ('delete', old.f_local_id, old.f_content);
END;

CREATE TRIGGER trg_fts_del AFTER DELETE ON t_chat_message
WHEN old.f_msg_type = 1 BEGIN
    INSERT INTO t_chat_message_fts(t_chat_message_fts, rowid, f_content)
    VALUES ('delete', old.f_local_id, old.f_content);
END;
