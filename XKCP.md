# XKCP —— 在 KCP 之上的扩展协议

XKCP 是 typhon 基于 [KCP](https://github.com/skywind3000/kcp) 的 **fork 改造**,不再追上游通用性。
原生 KCP 只解决**可靠传输**(有序、重传、流控、拥塞、分片重组),除此之外一概不管。
XKCP 把**连接管理 + 安全 + 保活**这些缺失的部分,直接做进协议层(`ikcp.c`),让上层
(`kcp::Server` / `kcp::Session`)只处理有效业务。

> 状态标记:✅ 已落地(代码已在 `src/kcp/ikcp.c` / `include/kcp/ikcp.h`) / 🔲 计划中(已设计、未写代码)

---

## 1. 原生 KCP 不做、XKCP 补上的事

| 能力 | 原生 KCP | XKCP | 状态 |
|---|---|---|---|
| 可靠传输(ARQ/窗口/分片) | ✔ | 沿用 | ✅ |
| 消息完整性/校验 | 只有 conv/长度/cmd 结构性检查,无完整性 | envelope SipHash(每包) + AEAD(整条消息) | SipHash✅(在用) / AEAD🔲 |
| 鉴权 | 无 | X25519 ECDH + token | 🔲 进协议层 |
| 连接握手 | 无(conv 需带外约定后直接收发) | `REGIST_REQ`/`REGIST_RSP` | ✅ |
| 断开 / 复位 / 踢人 | 无(`ikcp_release` 只释放本地,不通知对端) | `RST`(顶号) / `KIC`(主动踢) | ✅ |
| 保活 / 空闲超时 | 仅"重传耗尽判死"(`dead_link`),空闲检测不了 | `PING`/`PONG` + 空闲超时 → `on_timeout` | ✅ |

设计原则:**凡是出现在 `ikcp.c` 里的 cmd 都属于协议层**;回调只是"向上抛一下",
上层不实现回调时协议层也能自洽收尾。

---

## 2. 线格式(Wire format)

**KCP 段头沿用原生 24 字节,未扩展任何字段**(`conv`/`ts` 仍是 4B):

```
conv(4) | cmd(1) | frg(1) | wnd(2) | ts(4) | sn(4) | una(4) | len(4)   = 24B (IKCP_OVERHEAD)
[payload ...]
```

每个 UDP 包外层再裹一个 **envelope**(8B SipHash-2-4 tag,`ENVELOPE_MAC_LEN=8`):

```
UDP payload = [ SipHash tag 8B ] [ 一个或多个 KCP 段 ]
KCP_MTU     = UDP_MTU - 8
```

- envelope **入向**校验在 **XDP 内核态**(廉价挡 DoS);**出向**在 `Server::output` 里算
  (`siphash24(buf, KCP_HDR_LEN, Conf::siphash())`,覆盖 KCP 头、用服务端全局 key)。
- 控制段(REGIST/RST/KIC/PING/PONG)各自独占一个 UDP 包。

---

## 3. 命令表(IKCP_CMD_*)

| 值 | 名称 | 方向 | 说明 | 状态 |
|---|---|---|---|---|
| 81 | PUSH | 双向 | 数据(原生) | ✅ |
| 82 | ACK | 双向 | 确认(原生) | ✅ |
| 83 | WASK | 双向 | 窗口探测(原生) | ✅ |
| 84 | WINS | 双向 | 窗口通告(原生) | ✅ |
| **85** | **REGIST_REQ** | 客户端→服务端 | 注册/重连握手请求(绕过 sn 窗口) | ✅ |
| **86** | **REGIST_RSP** | 服务端→客户端 | 握手应答(带服务端公钥) | ✅ |
| **87** | **RST** | 服务端→旧端 | 顶号复位(其他设备登录) | ✅ |
| **88** | **KIC** | 服务端→客户端 | 主动踢人(admin) | ✅ |
| **89** | **PING** | 双向 | 保活心跳(空闲自动发) | ✅ |
| **90** | **PONG** | 双向 | 心跳应答 | ✅ |

---

## 4. 控制段的"早分发"(关键机制) ✅

新增的控制 cmd 在 `ikcp_input` 里**解析完段头、在 `ikcp_parse_una`/sn 窗口逻辑之前**就被分发,
处理完直接 `continue`:

```
解析段头 → 若 cmd ∈ {REGIST_REQ, REGIST_RSP, RST, KIC, PING, PONG}:
              按 cmd 处理(去重/回调/PONG…) → data+=len; size-=len; continue;
           否则走原生 PUSH/ACK/... 的 sn 窗口逻辑
```

**为什么必须早分发**:重连时客户端是全新 ikcp(`sn` 从 0 起),而服务端旧 ikcp 的
`rcv_nxt` 很高;走 sn 窗口的话新客户端的段会被当"早收过"丢弃。把 REGIST 做成绕过 sn
窗口的控制 cmd,重连握手才进得来。同时绕过 `ikcp_parse_una`,避免用新客户端的 `una`
误释放服务端 `snd_buf` 里的段。

---

## 5. 连接握手 + 传输层去重 ✅

### 握手
- 客户端发 `REGIST_REQ`(裸 cmd,不占 `sn`、不进 `snd_buf`,故**无 KCP 重传**;
  客户端**自己定时重发**直到收到 `REGIST_RSP`)。payload 头部是客户端 X25519 临时公钥。
- 服务端 `on_regist` 回调里:验 token、ECDH、(顶号则先 `RST` 旧端 + `ikcp_reset`)、
  把 `REGIST_RSP` 应答字节写进 `out_data`。
- `REGIST_RSP` 同为协议层 cmd(**不**降级成应用层 PKID 包,否则握手漏进应用层=耦合),
  客户端 `on_regist_rsp` 收。

### 传输层去重(幂等)
握手的重发由**协议层吃掉**,`on_regist` 只在"真·新握手"时触发:

- 握手 id = `REGIST_REQ` payload 头部 `IKCP_REGIST_ID_LEN`(=32)字节(= 临时公钥,ikcp 当**不透明**`memcmp`)。
- 缓存在 ikcpcb:`regist_id` / `has_regist` / `regist_rsp` / `regist_rsp_len`。
- **同 id**(重发)→ 直接重发缓存的 `regist_rsp`,**不调** `on_regist`。
- **异 id**(新握手)→ 调 `on_regist` 拿回 RSP → 缓存 id + RSP → 发出。
- 可靠性:RSP 是裸 cmd 无重传,靠"客户端重发 REQ → 协议层重发缓存 RSP"这个环兜底。

> 为什么用公钥不用 `ts`:`ts` 默认每次重传都被刷新(`ikcp_flush` 里 `seg->ts = current`),
> 做不了去重标识;而客户端临时公钥在一次握手内天然稳定、跨握手天然不同,且是 32 字节强标识。

### `ikcp_reset()` ✅
复位传输状态(清空四个队列 + 序号/窗口/RTT/保活计时),但**保留 `conv` 和全部配置**。
用于重连/顶号——让一个全新客户端重新被接受。

---

## 6. 重连 / 顶号 / 踢人 ✅

### 顶号(设备 B 登录,把设备 A 挤下线)
1. 设备 B 发 `REGIST_REQ`(新公钥),落到同一 worker(conv 路由确定性)。
2. 服务端 `on_regist`(异 id=新握手):验 token → 给**设备 A 旧地址**发 `RST` → `ikcp_reset`
   → 切会话地址到 B → 发 `REGIST_RSP` 给 B。
3. 设备 A 收 `RST` → `on_rst` 回调 → 弹"账号在别处登录"、断开。

`RST` 是 best-effort(收不收无所谓);真正的"顶号生效"靠 token 的有效期/新旧(last-login-wins),
不依赖 `RST` 一定送达。

### 主动踢人
服务端发 `KIC` → 客户端 `on_kic` 回调 → 弹"被踢下线"、断开。

> RST 与 KIC 都用回调把"原因"交给上层显示;两者传输行为一致(关连接),差别只在 UX 文案。

---

## 7. 保活 / 空闲超时 ✅

- ikcpcb 新增:`last_snd_ms_` / `last_rcv_ms_`(用 ikcp 的 `current` 32bit 时钟)、
  `ping_interval` / `dead_timeout`(0 = 关闭)、`dead`(防重复触发)、`on_timeout` 回调。
- `ikcp_output` 统一刷新 `last_snd_ms_`;`ikcp_input` 入口刷新 `last_rcv_ms_`。
- `ikcp_update` 每个 tick:
  - `current - last_rcv_ms_ > dead_timeout` → 置 `dead`、触发一次 `on_timeout`(上层摘会话);
  - 否则 `current - last_snd_ms_ > ping_interval` → 发 `PING`(发送会刷新 `last_snd_ms_`)。
- 收 `PING` → 回 `PONG`;收 `PONG` → 不处理(`last_rcv_ms_` 入口已刷新)。
- `ikcp_check` **无需改动**:两端 tick 间隔已被 cap 在 `interval`(10ms),远密于秒级保活。
- 与原生 `dead_link`(重传耗尽判死)互补:`dead_link` 只在有数据要发时才触发,`PING`/超时
  补上**空闲**那段。

---

## 8. 加解密进协议层 —— ✅ ikcp 内已实现 / 🔲 握手接线 + C++ 整合待做

把 ChaCha20-Poly1305 / X25519(crypto_kx)**直接做进 `ikcp.c`、直链 libsodium、不走
`utils/cryptor.hpp`**(cryptor 是 C++,ikcp.c 是 C 用不了)。**必须与现有 cryptor 字节级一致**,
否则 lilith C# 客户端连不上。

**✅ 已在 ikcp 落地**:`ikcp_make_nonce`、`ikcp_kx_keygen`/`ikcp_kx_server`/`ikcp_kx_client`;
`ikcp_send` 改成 AEAD wrapper(有 key 整条加密后交 `ikcp_send_raw` 分片)、`ikcp_recv` 合并后
原地解密(失败返回 -4);ikcpcb 加 `tx_key/rx_key/eph_pk/eph_sk/last_snd_seq_/last_rcv_seq_/
snd_dir/rcv_dir/has_key`;`ikcp_reset` 作废密钥。**`has_key` 默认 0 → 全程 dormant,不改变现状**。

**🔲 待做**:① 握手里调 kx(server `on_regist` 验 token 后 `ikcp_kx_server`、把 server_pk 放进 RSP;
client 发 REQ 前 `ikcp_kx_keygen`、收 RSP 调 `ikcp_kx_client`);② 撤掉 Session 自带加解密(否则双重);
③ C# 端镜像;④ kcp 编译目标链 `-lsodium`。

- **AEAD 粒度 = 整条消息**(在 `ikcp_send`/`ikcp_recv`,**不是每段**);每段只走 SipHash。
- nonce = `conv(4) | seq(4) | dir(1) | 0(3)`,`seq` 用**新的消息计数器**
  `last_snd_seq_` / `last_rcv_seq_`(按"消息"计,区别于按"段"的 `snd_nxt`)。
- 密钥 `tx_key` / `rx_key` 存 ikcpcb;`crypto_kx_server_session_keys` + `crypto_kx_keypair`
  (每握手生临时对,前向保密)。
- **铁律:每次 `ikcp_reset` 必须换 key**,否则 `seq` 清零 → 同 key 下 nonce 重用 → ChaCha20 崩。
- 待定子决定:整条 AEAD(头也加密,网关解密后按 `dst_id` 路由)vs 保留"Package 头明文 + payload AEAD"。
- token 校验仍留回调(要查账号系统)。

---

## 9. 回调一览(ikcpcb 函数指针)

| 回调 | 触发端 | 何时 | 备注 |
|---|---|---|---|
| `output` | 双向 | 原生:要发字节时 | 出向在此加 envelope | ✅ |
| `on_regist(…, out_data, out_len)` | 服务端 | 收到**新**`REGIST_REQ` | 回填 RSP;协议层缓存+发送+去重 | ✅ |
| `on_regist_rsp` | 客户端 | 收到 `REGIST_RSP` | 取服务端公钥、算密钥、置 authed | ✅ |
| `on_rst` | 客户端 | 收到 `RST` | 顶号下线 | ✅ |
| `on_kic` | 客户端 | 收到 `KIC` | 被踢下线 | ✅ |
| `on_timeout` | 双向 | 空闲超过 `dead_timeout` | 上层摘会话 | ✅ |

---

## 10. ikcpcb 新增字段

```c
/* 握手去重缓存 */               /* 保活 / 超时 */
char    regist_id[32];           IUINT32 last_snd_ms_;
int     has_regist;              IUINT32 last_rcv_ms_;
char    regist_rsp[256];         IUINT32 ping_interval;   /* 0=关 */
int     regist_rsp_len;          IUINT32 dead_timeout;    /* 0=关 */
                                 int     dead;
/* 计划中(crypto) */            void  (*on_timeout)(ikcpcb*, void*);
uint8_t tx_key[32];   🔲
uint8_t rx_key[32];   🔲
uint32_t last_snd_seq_; 🔲       /* AEAD 消息级 nonce 计数器 */
uint32_t last_rcv_seq_; 🔲
```

常量:`IKCP_REGIST_ID_LEN=32`、`IKCP_REGIST_RSP_MAX=256`。

---

## 11. 部署红线(C# 端 lockstep)

整套 XKCP 改动**必须和 lilith 的 C# `Kcp.cs` 端口同步**才能上线:
- C# 端 cmd 白名单要认 85–90,否则收到任何新 cmd → `return -3` → 丢掉整个 input → 连接坏。
- 握手去重、AEAD(整条消息 + 同样的 nonce/crypto_kx/siphash)、PING/PONG 都要镜像、字节级一致。
- 在 C# 未跟上前,**保活别打开**(`ping_interval`/`dead_timeout` 保持 0),crypto 也别启用。

---

## 12. 实现进度

- ✅ 已落地:控制 cmd(85–90)、早分发、`REGIST` 握手 + 传输层去重 + `ikcp_reset`、
  `RST`/`KIC`、`PING`/`PONG` + 空闲超时 + `on_timeout`;**协议层加解密(AEAD per 消息 + crypto_kx +
  `last_snd_seq_`/`last_rcv_seq_` + `tx_key`/`rx_key`,dormant 待握手激活)**。
- 🔲 待做:把 kx 接进握手 + 撤掉 Session 自带加解密;C++ 接线(Session 设 `ping_interval`/
  `dead_timeout`/`on_timeout`,退休 `Session::check_timeout`);kcp 目标链 `-lsodium`;C# 端口镜像;
  (可选,推后)服务端用 `ikcp_check`/时间轮调度替代全量扫;SipHash 出向是否也搬进 ikcp(现仍在 `Server::output`)。
