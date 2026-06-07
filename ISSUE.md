# typhon 待修问题清单

代码审查后尚未修复的问题。优先级从上到下递减。

---

## P1 — 待 Phase 2 定型

### 1. KCP 无 conv 鉴权 → DDoS 矢量

**位置**: [src/kcp/server.cpp](src/kcp/server.cpp)（`on_udp_handle` 里 `get_session` 走 nullptr 分支直接 `Session::create`）

- 任意陌生 conv 直接 `Session::create` 加进 sessions_。
- 30 s timeout 才清，攻击者可在 30 s 内灌爆 sessions_ map。
- 必须加 conv 频率限制 / pre-auth。做自定义消息时一起处理。

---

### 6. 转发路径的内嵌字节序 —— `Pke` 递归翻 vs KCP 透传来的 net 序

**位置**: [include/core/package.hpp](include/core/package.hpp)（`Pke<O>` 的 `hton`/`ntoh` 递归翻外层+内嵌）

字节序已全栈 phantom 化(见 memory `project-typhon-pke-send-byteorder`)：`Pke` 的 `hton`/`ntoh` **递归翻外层+内嵌**，`Pke<Host>` 的语义是"外层+内嵌**整体** host 序"。对「**本地构造**」的包正确：
- 心跳（`Connector::update` 构造 `Pke<Host>`）✅
- 回程（`Session::send` 后端 host 序构造）✅

但「**转发**」(`on_data` 网关→后端)的内嵌来源不同，分两条实现路线：
- **路线 A 解密转发**：用 `recv_pk` 解出的 host 序 Package 转发 → 内嵌 host、外层 host，能直接套 `Pke<Host>`。代价：网关解密了 payload（多花 CPU + 端到端加密在网关终止）。
- **路线 B 零拷贝透传**：直接拿 KCP 收到的原始 wire（**net 序 + 加密 payload**）prepend PackageEx 头转发。这时内嵌是 net 序、外层是 host 序 —— **"外 host / 内 net"混合态**，**不能直接塞 `Pke<Host>` 然后 `hton`**（会把已经是 net 的内嵌又翻回去）。转发又是最高频路径。

现在没爆，是因为 `on_data` 转发还没实现。**接转发时定 A/B**：
- 选 B（推荐，网关只路由不解密、payload 透传更省更安全）→ 给转发单开一条"只翻外层"的发送路径，只 `htons/htonl` 那 3 个 PackageEx 字段，内嵌一字节不碰，绕过 `Pke` 的递归 `hton`。

---

## Bug / 容量（2026-06-08 全量审查）

### B1. `tcp::Server::sessions_[fd]` 用 fd 直接索引 → 实际容量远小于 `MAX_CONN`，超限直接 abort

**位置**: [include/tcp/server.hpp:218](include/tcp/server.hpp#L218)（`sessions_[MAX_CONN]` + `get/add/remove_session` 的 `ASSERT(fd < MAX_CONN)`）

`sessions_[MAX_CONN]`(2048) 用 **fd 号**当下标，但 fd 是进程全局分配的——`lfd`/`epfd`/`stop_evfd` + 每个 proc 的 `evfd` + 后端 Connector fd 等都占号。连接数接近 2048 时新 `accept` 的 fd 会 **>2048**，命中 `ASSERT` → **abort 整个进程**（硬崩，不是优雅拒绝）。`accept4` 返回的 fd 没有 `< MAX_CONN` 的保证。

- 改法候选：① `fd → Session` 的 `unordered_map`（容量不再受 fd 号约束）；② 保留数组但 `fd >= MAX_CONN` 时优雅 `close(fd)` 拒绝 + 把 `MAX_CONN` 调到远高于预期连接数。

### B2. `epfd_` 的 `epoll_ctl` 被主线程和 worker 线程分裂管理

**位置**: [src/tcp/server.cpp](src/tcp/server.cpp) `on_listen_handle`(主线程 ADD) vs [server.hpp:143](include/tcp/server.hpp#L143) `add_session` 里 on_connected 拒绝时的 DEL(worker)

主线程 `accept` 后 ADD，worker 在 `add_session`(on_connected 非 0)里 DEL 同一 fd。`epoll_ctl` 内核侧线程安全、当前时序也对(ADD 同步先于 notify)，但"epfd 增删"被切成两半跨线程，很脆弱，以后加重连/复用极易踩。建议把 epoll 增删收敛到单一线程。

### B3. `udp_bind`/`tcp_connect` 用 `find(':')`，`tcp_listen` 用 `find_last_of(':')`

**位置**: [src/core/typhon.in.cpp:10/142](src/core/typhon.in.cpp#L10) vs :77

三个解析 host 的函数对冒号处理不一致。IPv4 单冒号没事，但传 IPv6 字面量(`[::1]:port`)时 `find(':')` 会切错。当前 IPv6 未支持所以不爆，是埋着的不一致。

---

## 冗余 / 死代码（2026-06-08 全量审查）

### R4. `tcp::Session::user_data_` + `set_user_data`/`get_user_data` 全死

[include/tcp/session.hpp](include/tcp/session.hpp) — 无调用者。后来加的 `id_`/`set_id()` 取代了它，现在是死成员 + 死方法。删。

---

## 可以做的优化（不是 bug）

### 3. `RcvBuf::buf` 是堆指针，`SndBuf::buf` 是 inline 数组 —— 同名不同物

跨结构体看代码容易愣一下。命名分开（比如 SndBuf 用 `data[UDP_MTU]`）心智负担更轻。低优先级，目前不动。

### 4. `tcp::Session::sbuf_` 没 reserve

**位置**: [include/tcp/session.hpp](include/tcp/session.hpp)

`std::vector<uint8_t> sbuf_ {}` 默认 0 容量，第一次 insert 触发 alloc，grow 时 realloc + memcpy。等「sbuf_ 是热路径」实测确认后再考虑 reserve。

### 5. CPU affinity

PLAN 已挂着。网关 worker 绑核能稳定 tail latency，应当尽早做。

### O4. `on_udp_handle` / `on_serv_handle` 各持一块 thread_local 大 buf

[src/kcp/server.cpp](src/kcp/server.cpp) — `rbuf[PKG_MAX_LEN]`(64KB) + `rbuf[TCP_RBUF_SIZE]`(8KB) 每 worker 线程各一份，worker 多时累加。都是一次性 thread_local，可接受；worker 数很大时可考虑共用一块。低优先级。
