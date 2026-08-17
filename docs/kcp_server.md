# `kcp::Server` / `kcp::Worker` — 网关控制面与数据面

> 网关侧 UDP/KCP 终结点,**控制面与数据面分离**:
> `kcp::Server` 跑在主线程,拥有两个 BPF 程序、创建 N 个 Worker、跑控制面 epoll、向 etcd 注册;
> `kcp::Worker` 是每线程的数据面 actor,独占**一条线程 + 一个 `SO_REUSEPORT` UDP socket**。
> `sk_reuseport` 按 KCP `conv % N` 路由,同 `conv` 永远落同一个 Worker → **share-nothing,全程无锁**。
> 源码:[include/kcp/server.hpp](../Adam/include/kcp/server.hpp) · [include/kcp/worker.hpp](../Adam/include/kcp/worker.hpp) · [src/kcp/worker.cpp](../Adam/src/kcp/worker.cpp)

---

## 1. 谁拥有什么

| | `kcp::Server`(主线程,控制面) | `kcp::Worker`(每线程,数据面) |
|---|---|---|
| epoll | `epfd_` —— 只挂 `evfd_` | `epfd_` —— UDP socket + eventfd + 后端 TCP fd |
| BPF | `EnvelopeFilter`(XDP)+ `Router`(sk_reuseport) | 无 |
| 会话 | 无 | `sesss_`:`flat_hash_map<conv, Session::Ptr>` |
| 后端 | 无 | `servs_`:`flat_hash_map<id, Connector::Ptr>` |
| 事件 | `evque_`(MPSC)+ `evfd_`,收 Worker 回流 | `mque_`(MPSC)+ `evfd_`,收主线程/他 Worker |
| 其他 | 线程池、etcd 注册、PID→handler 表 | 发送队列、Datagram 对象池、`RouterIDSet` |

**会话只按 `conv` 存,没有 user→session 映射。** 早期的 `users_` 已删 —— 有了 `conv % N == user_id % N` 不变量,由 uid 反查会话等价于算一次取模再查 `sesss_`,多一张表只会多一份要同步的状态。

---

## 2. 启动顺序(不能换)

```
EnvelopeFilter attach (XDP)  →  socket bind  →  Router attach (sk_reuseport)
```

XDP 必须**先于 socket 生效**,否则 attach 之前的那个窗口里,伪造包会直接进 socket 队列。

---

## 3. Worker 的 epoll:三类 fd

| fd | 触发模式 | 用途 |
|---|---|---|
| `ufd_`(UDP) | **故意非 ET** | `recvmmsg` 限量收包;有残留下轮再唤醒 → 防攻击者灌包把 Worker 钉死在一次 `while(read)` 里 |
| `evfd_`(eventfd) | ET | 主线程 / 其他 Worker 经 MPSC `mque_` 投递消息后敲这个 fd |
| serv fd(后端 TCP) | ET | `Connector` 的连接进度与收包 |

> 后端 fd 上的 `EPOLLOUT` **常挂着不摘**,这是给背压预留的(ET 前提下),不是冗余,别当无用代码删掉。

---

## 4. 收包路径(客户端 → 网关)

**三层包校验分工明确,不重复做:**

| 层 | 校验什么 |
|---|---|
| XDP(`envelope.bpf.c`) | 槽位 SipHash MAC + 长度下界。**协议无关**,不认识 KCP |
| `sk_reuseport`(`kcp.bpf.c`) | 读偏移 `+8` 的 `conv` 分流到 Worker;`conv == 0` 直接丢 |
| userland | **不重复校验 MAC** —— 直接偏移 8B 取信封,解 AEAD,喂 `ikcp_input` |

- 协议错(解密失败 / 越界 / 重放)→ 丢包;严重错误 → `remove_session`。
- **后端找不到或写失败不踢客户端** —— 回一个 `PID_TER_ERROR` 让客户端知道,连接留着。

---

## 5. 发包路径(网关 → 客户端)

KCP 分片后经 `output` 回调 → 取一个 `Datagram`(对象池)→ 封信封(AEAD + 计数器)→ 盖槽位 MAC → 进发送队列,`update()` 末尾 `sendmmsg` 批量出网卡。

**握手期(密钥未就绪)格式是 `[8B MAC][裸 KCP 数据报]`** —— 槽位 MAC 任何时候都要盖,否则 XDP 会把整个握手丢光。

---

## 6. 鉴权握手(`on_regist_req`)

校验链一步都不能少:

```
sealedbox 解密 → 验 expire → 验 conv == s->conv() → 验 token.user_id == data.src_id
  → 验 conv % N == user_id % N 不变量 → ed25519 验签 → crypto_kx 派生双向密钥 → 回 RSP
```

`AccessToken` 由 Eva 签发(116B),客户端**只搬运不解密**。RSP 回带网关的临时 X25519 公钥,两端 X25519 皆临时 → **双边前向保密**。详见 [login.md](login.md)。

---

## 7. 终端进入后端(ENT)

`PID_TER_ENT_REQ` 是唯一一个**客户端发出、但网关绝不转发**的包:

1. 客户端发 `TER_ENT_REQ`(`dst_id` = 目标后端 id,**payload 空**),只是个扳机。
2. 网关 `on_terminal_enter_req`:验 authed → **拒绝直接进路由服务**(那会导致顶号裂脑)→ 验后端就绪 → 用**会话自己的状态**重新盖章后发给后端。
3. 后端 `add_terminal` → `TER_ENT_RSP` 回网关。
4. 网关 `on_terminal_enter_rsp`:成功则 `bind()`,再把 RSP 中继给终端。

客户端伪造不了身份,因为盖章用的是网关侧会话状态,不是包里的字段。断线重连会自动重报;后端重启由 `terminal_reenter` 覆盖(路由服务按 `uid % N`,其余按 `binds_` 判定)。

---

## 8. `update()` 周期(每轮 epoll 尾)

单线程顺序执行三件事:

1. **session 超时 + KCP tick**:遍历 `sesss_`,`ikcp_update` 推进重传 / ACK / flush。**判死统一看返回码**(`-1` 超时 / `-2` 收到 RST),上游的 `state` / `dead_link` 已删。
2. **发送队列出网卡**:过期的直接释放不发,其余 `sendmmsg` 批量发,合并成一次 erase。
3. **后端心跳 / 判死**:遍历 `servs_`,`Connector::update` 发 PING、按 `last_send` / `last_recv` 两级超时判死;判死则摘除并经 `evque_` **回流**通知主线程(详见 [typhon_server.md](typhon_server.md))。

> `ikcp_update` 目前是每轮全量跑。接 `ikcp_check` 之后才谈得上把它挪进时间轮 —— [utils/TimingWheel](../Adam/include/utils/) 适合超时这类一次性稀疏事件,不适合周期 tick。

---

## 9. 后端连接(`on_serv_handle`)

`servs_` 里每个 `Connector` 是到一个后端 instance 的 TCP 长连:

- `EPOLLOUT` + `Connecting` → `getsockopt(SO_ERROR)` 确认连上 → `regist()` 发 `BKD_REG_REQ`。
- `EPOLLIN` → 读 TCP 流 → `frame_decode` 切整帧 → `on_pong` / `on_regist_rsp` / `on_terminal_enter_rsp` / **`on_s2c`**。
- `EPOLLERR/HUP` → `remove_serv`(摘除 + pipe 回流通知主线程,由服务发现层重连)。

**`Connector` 刻意不做 TCP 层自重连** —— 重连是服务发现的事(etcd 里那个后端可能已经彻底没了),放在这一层会和 `update_serv` 打架。

`on_s2c` 回程按 `meta.conv % N` 判归属:本 Worker 直发,跨 Worker 则 COPY 一份转给 owner Worker。

---

## 10. 已知短板

- **`on_ensure_backend` 不刷新已有 Connector 的 PID 集合** —— 后端新增一个 PID 后,网关仍用旧集合判断,会回 `PERR_REQ_NOT_ACCEPT`("客户端版本过低")。当前只能重启网关。
- `ifname` 默认 `lo`,上线不改成实际网卡的话,**XDP 防护静默失效**(安全性不受影响,用户态是权威,但防护是空的)。
