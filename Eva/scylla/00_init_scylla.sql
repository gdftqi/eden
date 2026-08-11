-- ============================================================
-- Keyspace
-- ============================================================
CREATE KEYSPACE IF NOT EXISTS eva
WITH replication = {
    'class': 'NetworkTopologyStrategy',
    'datacenter1': 1
}
AND tablets = {
    'enabled': false
};


-- ============================================================
-- 消息主表
-- ============================================================
CREATE TABLE IF NOT EXISTS eva.chat_message (
    chat_id    bigint,      -- 会话 ID. 
    bucket     int,         -- 分桶号, 防止单会话 partition 无限增长.
    seq        bigint,      -- 会话内单调序号. 排序、已读判定、增量同步的唯一依据
    msg_id     bigint,      -- 全网唯一 ID(雪花). 撤回、引用、编辑目标、举报都用它
    cli_uuid   text,        -- 客户端生成的幂等 ID.
    from_id    bigint,      -- 发送者 uid
    to_id      bigint,      -- 单聊对端 uid, 群聊填 0.
    msg_type   tinyint,     -- 1文本 2图片 3文件 4语音 ... 100编辑事件 101撤回事件
    content    text,        -- 文本
    target_id  bigint,      -- 仅事件行使用
    edit_seq   bigint,      -- 最后一次编辑事件的 seq,兼作版本号
    edited_at  timestamp,
    is_revoked boolean,     -- 撤回标记
    created_at timestamp,   -- 服务端时间,不采信客户端时钟
    PRIMARY KEY ((chat_id, bucket), seq)
) WITH CLUSTERING ORDER BY (seq DESC)   -- 倒序:拉最新消息无需反向扫描
AND compaction = {
    'class': 'TimeWindowCompactionStrategy',
    'compaction_window_unit': 'DAYS',
    'compaction_window_size': 7
}
AND compression = {
    'sstable_compression': 'LZ4Compressor'
}
AND default_time_to_live = 0;


-- ============================================================
-- 编辑历史(可选)
-- ============================================================
CREATE TABLE IF NOT EXISTS eva.msg_edit_history (
    msg_id    bigint,
    edit_seq  bigint,     -- 版本号,倒序即最新在前
    content   text,       -- 该版本的内容
    editor_id bigint,
    edited_at timestamp,
    PRIMARY KEY (msg_id, edit_seq)
) WITH CLUSTERING ORDER BY (edit_seq DESC);


-- ============================================================
-- 号段水位
-- ============================================================
-- 发号器的持久台账: 记"已承诺(批发)到哪", 不是"已使用到哪"(后者无表, 见上方墓碑).
-- 副业: 冷会话拉最新页拿 allocated/10000 当起点桶上界(普通 SELECT, 空桶兜底).
-- 铁律: 除副业的 SELECT 外只准 LWT 碰 -- 普通写和 Paxos 混同一分区
--       会破坏线性化保证, 坏起来无声无息. cqlsh 里也别手改
CREATE TABLE IF NOT EXISTS eva.chat_seq (
    chat_id   bigint PRIMARY KEY,
    allocated bigint      -- 已分配上限。用 LWT (IF allocated = ?) 原子抬高
                          -- CAS 失败即说明归属权已转移,天然充当 fencing token
                          -- 步长动态增长(32 起步,上限 4096):冷会话少浪费、热会话摊薄 LWT 开销
) WITH compaction = {'class': 'LeveledCompactionStrategy'};


-- ============================================================
-- 用户会话游标(兼会话列表)
-- ============================================================
-- 按 user_id 分区,一次查询拉到该用户全部会话
CREATE TABLE IF NOT EXISTS eva.chat_cursor (
    user_id          bigint,
    chat_id          bigint,
    read_seq         bigint,   -- 已读位置。某条消息已读 = 其 seq <= read_seq
    recv_seq         bigint,   -- 已送达位置。重连时上报,服务端补推差集
    clear_before_seq bigint,   -- "清空聊天记录"水位,一条记录顶掉海量删除标记
    is_pinned        boolean,
    is_muted         boolean,
    joined_at        timestamp,
    PRIMARY KEY (user_id, chat_id)
) WITH compaction = {'class': 'LeveledCompactionStrategy'};


-- ============================================================
-- 会话成员
-- ============================================================
CREATE TABLE IF NOT EXISTS eva.chat_member (
    chat_id  bigint,
    user_id  bigint,
    role     tinyint,     -- 1普通成员 2管理员 3群主
    join_seq bigint,      -- 入群时的 seq, 之前的消息不给他看
    state    tinyint,     -- 1在群 -1已退出
    PRIMARY KEY ((chat_id), user_id)
) WITH compaction = {
    'class': 'LeveledCompactionStrategy'
};


-- ============================================================
-- 单条删除标记
-- ============================================================
-- 每用户视角的删除,与撤回无关(撤回是全局的,走事件)
-- 若产品只有"清空聊天记录"、没有"删除单条",整张表可砍掉
CREATE TABLE IF NOT EXISTS eva.chat_deleted (
    user_id bigint,
    chat_id bigint,
    seq     bigint,
    PRIMARY KEY ((user_id, chat_id), seq)
);