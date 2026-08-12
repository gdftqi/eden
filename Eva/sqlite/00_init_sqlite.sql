-- ============================================================
-- 连接参数
-- ============================================================
-- 每个账号一个独立 db 文件:im_<uid>.db,多账号切换直接换文件

-- 写进文件、以后自动生效, 只跑这一次就够
PRAGMA journal_mode = WAL;        -- 读写不互斥,收消息不阻塞列表渲染
PRAGMA user_version = 1;          -- schema 版本,启动时比对决定跑哪些迁移脚本

-- !! 下面这些是连接级的, 不写进文件 !!
-- 建库时跑一遍只对建库那条连接有效. 应用每次 open 之后都必须重放一遍,
-- 否则全是默认值 -- 尤其 foreign_keys 默认就是 OFF
PRAGMA synchronous  = NORMAL;     -- WAL 下足够安全;FULL 每条消息多一次 fsync
PRAGMA foreign_keys = ON;         -- 目前没有任何 FOREIGN KEY 约束, 空设; 留着以防后面加
PRAGMA temp_store   = MEMORY;
PRAGMA mmap_size    = 268435456;  -- 256MB
PRAGMA cache_size   = -8000;      -- 8MB


-- ============================================================
-- 会话列表
-- ============================================================
CREATE TABLE conversation (
    chat_id       INTEGER PRIMARY KEY,
    chat_type     INTEGER NOT NULL,           -- 1单聊 2群聊
    peer_id       INTEGER,                    -- 单聊对端 uid / 群 ID
    server_seq    INTEGER NOT NULL DEFAULT 0, -- 服务端当前最大 seq(含编辑等事件)
    recv_seq      INTEGER NOT NULL DEFAULT 0, -- 本地已消费到的最大 seq,重连时上报
                                              -- 事件行虽不显示,也要推进它,否则会重复拉取
    read_seq      INTEGER NOT NULL DEFAULT 0, -- 本端已读位置
    peer_read_seq INTEGER NOT NULL DEFAULT 0, -- 对方已读到哪,用于显示"已读"
    unread_count  INTEGER NOT NULL DEFAULT 0, -- 冗余存储,会话列表页不能做 COUNT
                                              -- 不能用 server_seq - read_seq 算:编辑事件会被计成未读
                                              -- 收到可见消息时 +1,以服务端下发值为准做校正
    last_preview  TEXT,                       -- 摘要冗余,避免每次 join message
                                              -- 若被编辑的正是最后一条,这里要同步更新
    last_time     INTEGER NOT NULL DEFAULT 0, -- 最后一条消息时间(毫秒)
    sort_time     INTEGER NOT NULL DEFAULT 0, -- 排序用,与 last_time 分开
                                              -- 置顶、存草稿会改排序但不该改消息时间
    clear_before_seq INTEGER NOT NULL DEFAULT 0, -- "清空聊天记录"水位,与服务端 chat_cursor 同名列对应
                                              -- seq <= 它的消息一律不显示,不然换设备登录旧消息会全回来
    draft         TEXT,
    is_pinned     INTEGER NOT NULL DEFAULT 0,
    is_muted      INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX idx_conv_sort ON conversation(is_pinned DESC, sort_time DESC);


-- ============================================================
-- 消息
-- ============================================================
-- 只存可见消息。编辑/撤回事件拉下来后应用到目标行即可,不在此建行
CREATE TABLE message (
    local_id   INTEGER PRIMARY KEY AUTOINCREMENT,
    chat_id    INTEGER NOT NULL,
    seq        INTEGER,              -- 服务端序号。未确认时为 NULL
    msg_id     INTEGER,              -- 服务端全局 ID。未确认时为 NULL
    client_id  INTEGER NOT NULL,     -- 幂等 ID: 自己发的 = 本行 local_id;
                                     -- 收到的 = NTF 里发送方的 cli_id.
                                     -- 各发送方各自递增会撞, 唯一索引必须带 from_id
    from_id    INTEGER NOT NULL,
    msg_type   INTEGER NOT NULL,     -- 1文本 2图片 3文件 4语音 ...
    content    TEXT,                 -- 当前内容(编辑后即新内容)
    status     INTEGER NOT NULL DEFAULT 0,  -- 0发送中 1成功 2失败
    sort_key   INTEGER NOT NULL,     -- 排序键。已确认 = seq * 1000
                                     -- 未确认 = (1 << 40) + local_id
                                     -- 让未确认消息稳定排在末尾,ACK 后改成 seq*1000 归位
                                     -- 编辑绝不能改动它:消息要留在原位置
                                     --
                                     -- 基数用 1<<40 而不是"当前最大 seq * 1000 + n":
                                     -- 后者会被随后到达的对方消息越过去 --
                                     --   本地 max seq=100, 我发的转圈中 = 100001,
                                     --   对方 seq=101 到达 = 101000 > 100001, 插到我下面,
                                     --   等我的 ACK(seq=102) 回来又跳回最底, 界面抖一下.
                                     -- 1<<40 远大于任何 seq*1000, 待确认的天然恒在底部
    edit_seq   INTEGER NOT NULL DEFAULT 0,  -- 已应用的最后一次编辑事件 seq,兼作版本号
                                            -- 仅当新事件 edit_seq 更大时才应用,防乱序回退
    edited_at  INTEGER,              -- 有值即显示"已编辑"标记
    is_revoked INTEGER NOT NULL DEFAULT 0,  -- 撤回后置 1,content 清空,渲染成占位提示
    created_at INTEGER NOT NULL,     -- 毫秒
    is_deleted INTEGER NOT NULL DEFAULT 0   -- 本端删除(仅自己不可见)
);
CREATE UNIQUE INDEX idx_msg_client ON message(chat_id, from_id, client_id);
-- ↑ 幂等基石:重发、重连、重复推送靠 INSERT OR IGNORE 天然去重
CREATE UNIQUE INDEX idx_msg_seq ON message(chat_id, seq) WHERE seq IS NOT NULL;
-- ↑ 部分索引:未确认消息 seq 为 NULL,不加条件唯一约束建不起来
CREATE INDEX idx_msg_sort ON message(chat_id, sort_key DESC, local_id DESC);
CREATE INDEX idx_msg_id   ON message(msg_id) WHERE msg_id IS NOT NULL;
-- ↑ 编辑/撤回事件只带 msg_id,需要靠它定位目标行


-- ============================================================
-- 待发队列
-- ============================================================
-- 发送、编辑、撤回等待确认的操作。App 被杀后重启要能继续重试
-- 不放在 message 表里:编辑和撤回没有对应的消息行
CREATE TABLE outbox (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    chat_id    INTEGER NOT NULL,
    op_type    INTEGER NOT NULL,     -- 1发送 2编辑 3撤回 4已读上报
    client_id  INTEGER NOT NULL,     -- 幂等 ID(= message.local_id), 重试时保持不变
    target_id  INTEGER,              -- 编辑/撤回的目标 msg_id
    payload    TEXT,                 -- 新内容等参数
    prev_value TEXT,                 -- 操作前的旧值,失败时回滚本地乐观更新
    retry_cnt  INTEGER NOT NULL DEFAULT 0,
    created_at INTEGER NOT NULL
);
CREATE INDEX idx_outbox_chat ON outbox(chat_id, id);


-- ============================================================
-- 空洞记录
-- ============================================================
-- 离线期间漏收的区间。往上翻页时据此判断该读本地还是拉服务端
-- 没有这张表,历史消息一定会出现静默丢失和跳跃
-- 注意:seq 本身允许有空洞(服务端号段浪费),
-- 区间是否补齐以服务端返回的"该区间已完整"为准,不能靠 seq 连续性推断
CREATE TABLE message_gap (
    chat_id   INTEGER NOT NULL,
    start_seq INTEGER NOT NULL,   -- 缺失区间 [start_seq, end_seq]
    end_seq   INTEGER NOT NULL,
    PRIMARY KEY (chat_id, start_seq)
);


-- ============================================================
-- 资料缓存
-- ============================================================
CREATE TABLE user_cache (
    user_id  INTEGER PRIMARY KEY,
    nickname TEXT,
    avatar   TEXT,
    remark   TEXT,                          -- 本地备注,显示时优先于 nickname
    version  INTEGER NOT NULL DEFAULT 0     -- 服务端资料版本,用于增量拉取
);

CREATE TABLE group_member (
    chat_id INTEGER NOT NULL,
    user_id INTEGER NOT NULL,
    role    INTEGER,            -- 1普通 2管理员 3群主
    alias   TEXT,               -- 群昵称
    PRIMARY KEY (chat_id, user_id)
);


-- ============================================================
-- 杂项
-- ============================================================
CREATE TABLE kv (k TEXT PRIMARY KEY, v TEXT);   -- 全局同步位点、配置、登录态等


-- ============================================================
-- 全文搜索
-- ============================================================
CREATE VIRTUAL TABLE message_fts USING fts5(
    content,
    content='message',          -- 外部内容表,不重复存正文
    content_rowid='local_id',
    tokenize='trigram'          -- 中文必须用 trigram
                                -- 默认 unicode61 会把整句中文当成一个词,搜不出来
);

CREATE TRIGGER msg_fts_ins AFTER INSERT ON message
WHEN new.msg_type = 1 BEGIN     -- 只索引文本,图片 JSON 进去是浪费
    INSERT INTO message_fts(rowid, content) VALUES (new.local_id, new.content);
END;

CREATE TRIGGER msg_fts_upd AFTER UPDATE OF content ON message
WHEN new.msg_type = 1 AND new.is_deleted = 0 BEGIN
                                -- 编辑后必须重建索引,否则搜到的还是旧文本
                                -- 已本端删除的不再入索引, 否则又被搜出来
    INSERT INTO message_fts(message_fts, rowid, content)
    VALUES ('delete', old.local_id, old.content);
    INSERT INTO message_fts(rowid, content) VALUES (new.local_id, new.content);
END;

-- 本端删除只是打标, 不会触发 AFTER DELETE, 所以要单独摘索引;
-- 少了这个触发器, 全文搜索能搜出用户已经删掉的消息
CREATE TRIGGER msg_fts_del_flag AFTER UPDATE OF is_deleted ON message
WHEN new.msg_type = 1 AND new.is_deleted = 1 AND old.is_deleted = 0 BEGIN
    INSERT INTO message_fts(message_fts, rowid, content)
    VALUES ('delete', old.local_id, old.content);
END;

CREATE TRIGGER msg_fts_del AFTER DELETE ON message
WHEN old.msg_type = 1 BEGIN
    INSERT INTO message_fts(message_fts, rowid, content)
    VALUES ('delete', old.local_id, old.content);
END;