# CCS — 聊天服务

独立后端服务, 接在 Moses(KCP 网关)后面, 消息落 ScyllaDB。

与 Noah 的分工: Noah 是用户路由服务(路由表 / 顶号 / 踢旧设备), CCS 只管聊天业务。
它是 `regist_terminal` / BIND / 绑定集的**第二个**真实调用方 —— 一个客户端终端会同时绑
Noah 和 CCS 两个后端。

## 存储

| | 位置 | 用途 |
|---|---|---|
| 服务端 | ScyllaDB `eva` keyspace | 消息权威副本, 全历史 |
| 客户端 | SQLite `im_<uid>.db` | 本地档案 + 离线可读 |

表结构见 `Eva/scylla/00_init_scylla.sql` 和 `Eva/sqlite/00_init_sqlite.sql`。

新设备登录要能看到历史 —— 所以服务端是权威副本, 不是"投递完就删"的中转站。

---

## 核心概念

**seq** — 会话内单调序号, 服务端分配。排序、已读判定、增量同步全靠它。
**允许有空洞**(号段预留浪费), 客户端不得靠连续性推断是否漏消息。

**水位线** — 回执不逐条记, 每个 `(会话, 用户)` 只存两个数字:

```
chat_cursor(user, chat).recv_seq   已送达到哪   -> 灰双勾
chat_cursor(user, chat).read_seq   已读到哪     -> 蓝双勾
```

一次读 50 条只写一个数字, 而且它是**状态不是事件** —— 对端离线期间的回执不用补发,
上线读一次当前值就对了。

**client_id** — 客户端生成的幂等 ID。服务端靠它认出重发, 客户端靠它把
同步/ACK 回来的消息对上本地那条"发送中"。服务端下发消息时**必须随行带上它**。

---

## 发送流程

A 发给 B, 两端在线。

### 1. 客户端 A: 本地先落地

```
一个事务里写两张表:
  message(client_id, from_id, content, status=0,
          seq=NULL, msg_id=NULL, sort_key=(1<<40)+local_id)
  outbox (op_type=1, client_id, payload)
```

两条必须同事务 —— App 被杀重启后靠 outbox 接着重试。

UI 显示 🕐。

> `sort_key` 用 `1<<40` 当基数, 让待确认消息恒在列表底部。
> 不能用"当前最大 seq * 1000 + n": 随后到达的对方消息会越过去,
> 等自己的 ACK 回来又跳回底部, 界面抖一下。

### 2. 服务端: 落库

```
① 查进程内去重 LRU (每会话, client_id -> chat_id/seq/msg_id, 留 10 分钟)
   命中 -> 这是重发, 什么都不写, 把原来的结果再回一遍, 结束.
   不落表: 会话单属主, 重发必回同一进程; 进程重启丢内存由
   客户端"先同步再重发"那条硬规矩兜底(见重连同步一节)

② 取 seq
   内存号段有余量 -> 直接给
   用完了 -> LWT 抬高 chat_seq.allocated (IF allocated = ?)
            CAS 失败即说明会话归属权已转移, 兼作 fencing token

③ msg_id = 雪花

④ bucket = seq / 10000
   纯算式(Discord 同款思路): 每行占一个号 -> 每桶硬上界 1 万行,
   seq 空洞只会让桶不满, 无害. 任何人拿到 seq 都能直接定位分区, 不用查表.
   前提: 发号步长上限(4096)必须远小于桶宽(10000)

⑤ 写 chat_message((chat_id, bucket), seq)

⑥ 去重 LRU 记一笔: client_id -> (chat_id, seq, msg_id)
```

没有 msg_id 反查表: 编辑/撤回等指名道姓的请求一律带 (chat_id, seq, msg_id)
三件套 -- 发起方本地三样俱全。服务端 seq 直读, 再校验 行.msg_id 与
行.from_id, 零查表还多一道防护。

### 3. 服务端: 应答 + 扇出

```
⑦ 回 A: ACK { client_id, seq, msg_id, created_at }
⑧ 查 chat_member((chat_id)) 拿成员
⑨ 逐个问网关是否在线 -> 在线的推 Update, 不在线的只写不推
```

**没有独立的离线消息队列。** 写和推是分开的两步, 推丢了下次拉还在。

### 4. 客户端 A: 收 ACK

```
UPDATE message SET seq, msg_id, status=1, sort_key = seq*1000
       WHERE chat_id=? AND client_id=?
DELETE FROM outbox WHERE client_id=?
UPDATE conversation SET server_seq, recv_seq, last_preview, last_time, sort_time
```

UI 单勾 ✓ (含义是"服务端收到了", 不是"对方收到了")。

### 5. 客户端 B: 收推送

```
① 比对 conversation.recv_seq 判空洞 -> 缺就写 message_gap 并补拉
② INSERT OR IGNORE INTO message(...)     <- UNIQUE(chat_id, client_id) 天然幂等
③ UPDATE conversation: recv_seq, unread_count+1, last_preview/time/sort_time
④ FTS 触发器自动索引
⑤ 落库成功之后 才上报 Delivered
```

第 ⑤ 步的顺序是硬性的: 收到就报的话, B 崩溃没写进去, A 那边却已经显示送达。

### 6. 回执

```
Delivered -> chat_cursor(B, chat).recv_seq = seq   -> 推给 A -> 灰双勾 ✓✓
Read      -> chat_cursor(B, chat).read_seq = seq   -> 推给 A -> 蓝双勾 ✓✓
                                                   -> 也要写 B 自己的会话,
                                                      让 B 的其他设备红点消掉
```

A 离线时不推。A 重连读一次 `chat_cursor(对方uid, chat_id)` 就补齐 —— 水位线是状态,
不需要重放事件。

---

## 重连同步

客户端上报每个会话的游标, 服务端回**事件流**而不是消息流:

```
客户端 -> { chat_a: 1000, chat_b: 55, ... }
服务端 -> 读该用户 chat_cursor 分区拿到他的全部会话, 逐个:
          从 bucket = 游标/10000 开始读 seq > 游标, 向上走到空桶为止
          (没报过的会话游标按 0, 从 bucket 0 起)
```

没有"会话水位表", 判活就是探消息本体: 有就是有, 没有就是空读一次。
连"消息落了、水位没跟上"的崩溃孤儿都藏不住 -- 探的不是任何副本。
"空桶即终点"由分桶不变式保证: 空洞 < 步长上限 4096 < 桶宽 10000,
数据中间隔不出整空桶。

客户端还不知道的新会话不需要任何额外字段: 报不出游标 -> 按 0 算 ->
有过消息就必然被探到。

**硬规矩: 先同步, 后重发 outbox, 顺序不能反。**
服务端去重不落库(进程内 LRU), 重启后只认这一条: 同步拉回的消息带着原
`client_id`, 撞上本地 `UNIQUE(chat_id, client_id)` 就说明那条其实早发成功了
—— 认领它(回填 seq/msg_id, 打勾), 销掉对应 outbox 项, 不再重发。
注意认领要走 UPDATE 不是 INSERT OR IGNORE: 本地那行已存在(status=发送中),
IGNORE 会把同步来的 seq 悄悄扔掉。

历史消息走另一条路: `GetHistory(chat_id, offset, limit)`, 只在用户点开会话、
往上翻页时拉。

---

## 未定项

以下几条**动手前必须定**, 否则写到一半要返工:

**推送要带 `prev_seq`。** seq 允许有空洞, 所以 B 收到 `seq=101`、本地 `recv_seq=99` 时
无法判断 100 是被浪费的号还是真漏了。补拉路径有"服务端说该区间已完整"兜底,
实时推送路径没有。带上上一条实际落库的 seq, B 一比就知道接不接得上。
(等价于 Telegram 的 `pts_count`)

**`conversation` 缺 `peer_recv_seq`。** 客户端只镜像了 `peer_read_seq`(蓝勾),
灰双勾没地方存。

**`sort_key` 的写入时机。** `local_id` 是 AUTOINCREMENT, INSERT 那一刻还不存在,
`(1<<40) + local_id` 在同一条语句里写不出来。要么插入后立刻
`UPDATE ... SET sort_key = (1<<40) + last_insert_rowid()`, 要么客户端自己维护尾号。

**`outbox` 缺 `next_retry_at`。** 只有 `retry_cnt`, 没有下次重试时间。
重启后只能立刻全发一遍, 断网时会变成疯狂重试。

**tablets 与 LWT。** 整套发号挂在 LWT 上, 而 tablets 与 LWT 的兼容性随版本变化。
keyspace 目前显式关掉了 tablets, 建库后先验:
`INSERT INTO eva.chat_seq (chat_id, allocated) VALUES (1, 0) IF NOT EXISTS;`

---

## 第一版范围

8/31 演示。刻意砍掉的:

- 群聊 (表结构留了 `chat_member`, 但不实现)
- 会话列表落库 (放内存, 重启重拉)
- 无限往上翻页 (只拉最近 N 条)
- 图片缩略图 / 上传进度 (复用现有 `Upload` 接口, 消息体只存 url)

保留双勾 —— 演示里最直观, 成本也低。
