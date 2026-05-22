# typhon 待修问题清单

代码审查后尚未修复的问题。优先级从上到下递减。

---

## P0 — 真 BUG

### 1. `tcp::Server::run()` 的 on_init 失败路径会让进程 `std::terminate`

**位置**: [src/tcp/server.cpp:16-21](src/tcp/server.cpp#L16-L21)

```cpp
init();                              // 已经创建了 workers_ 和 threads_,worker 线程在 epoll_wait
if (event_->on_init(this) != 0) {
    release();                       // ← release 内部 workers_.clear() + threads_.clear()
    state_.store(core::State::Stopped);
    return;
}
```

`release()` 里 `workers_.clear()` → `~Worker()` → close worker.epfd_，但 worker 线程还在 epoll_wait 上 → UB。接着 `threads_.clear()` → std::thread 析构 joinable 线程 → **`std::terminate()`**。

只要用户的 on_init 返回非 0，server 立即 abort。

**修法**: on_init 失败时先 stop+join worker 再 release：
```cpp
if (event_->on_init(this) != 0) {
    for (auto& w: workers_) w->stop();
    for (auto& t: threads_) t.join();
    release();
    state_.store(core::State::Stopped);
    return;
}
```

或者把 worker 创建从 `init()` 挪到 `on_init` 成功之后。

---

### 2. `tcp::Session::send` 大包尺寸校验上限错了

**位置**: [src/tcp/session.cpp:19-22](src/tcp/session.cpp#L19-L22)

```cpp
int total = pk->pk_len;
if (total > core::PKG_MAX_LEN - core::PKG_TAIL_LEN) {  // = 65527
    return -1;
}
```

backend 方向 pk_len **已含 tail**，合法范围 `[HEADER+TAIL, PKG_MAX_LEN] = [20, 65535]`。`pk_len ∈ [65528, 65535]` 的合法包会被误拒。

**修法**:
```cpp
if (total < core::PKG_HEADER_LEN + core::PKG_TAIL_LEN || total > core::PKG_MAX_LEN) {
    return -1;
}
```

---

### 3. pidfile 不 truncate，PID 变短时残留旧字节

**位置**: [include/utils/sys.hpp:35-40](include/utils/sys.hpp#L35-L40)

`write` 从 offset=0 开始但不截断。旧 PID `12345`（5 字节），新进程 PID `789`（3 字节）→ 文件内容变成 `78945`。外部读 pidfile 的工具会拿到错误 PID。

**修法**: 拿到锁之后 `::ftruncate(fd, 0)` 再 write。

---

## P1 — 设计 / 时序

### 4. KCP 无 conv 鉴权 → DDoS 矢量

**位置**: [src/kcp/server.cpp:194-197](src/kcp/server.cpp#L194-L197)

- 任意陌生 conv 直接 `Session::create` 加进 sessions_。
- 30 s timeout 才清，攻击者可在 30 s 内灌爆 sessions_ map。
- 必须加 conv 频率限制 / pre-auth。属于 Phase 1 跑通后的强需求，做自定义消息时一起处理。

---

### 5. `tcp::Session::recv` 的 idem 校验对「gateway 多路复用」架构不对

**位置**: [include/tcp/session.hpp:91-93](include/tcp/session.hpp#L91-L93)

```cpp
if (rcv_idem_ >= (*pk)->pk_idem) {
    return 0;
}
```

`rcv_idem_` 是 **session 维度**单调计数器。如果 gateway 多路复用多个 client 进同一个 backend session：client A idem=100、client B idem=50 → backend 把 B 的包当重复丢弃。

- 如果架构是「gateway 对每个 client 在 backend 维护独立 TCP 连接」（1:1 而非多路复用），现在就对；
- 如果是多路复用，要按 `(src_id, idem)` 联合校验，或者干脆不在 backend 做。

需配合 Phase 2 网关感知路由设计明确。

---

### 6. KCP `add_session` 拒绝时栈上还在用 `s` 处理本次包

**位置**: [src/kcp/server.cpp:196-222](src/kcp/server.cpp#L196-L222)

```cpp
auto s = get_session(conv);
if (s == nullptr) {
    s = Session::create(...);
    add_session(conv, s);     // on_connected 返回 != 0 时 sessions_.erase(conv)
}

if (s->input(...)) continue;       // ← 即使被拒,栈上 s 还活,继续处理

while (true) {
    ...
    event_->on_data(s, pkg);       // ← 业务被通知一个「已拒绝」的 session
}
```

被拒的连接还能让业务收到一次 on_data；KCP 内部 flush 出的 SndBuf 也会进 sque_。

**修法**: 让 `add_session` 返回 bool，调用方判断后 `continue` 跳过本次包。

---

### 7. `typhon::Server::run()` 失败路径不 clear `servers_`

**位置**: [src/typhon.cpp:25-44](src/typhon.cpp#L25-L44)

中间任意一步失败直接 `return`，`servers_` 留着上次的残骸。重试 `run()` 时 state CAS 通过、走 for 循环又 emplace 一遍，servers_ 越攒越多。

**修法**: 失败路径加 `servers_.clear(); threads_.clear();`，或 ASSERT 让重启起新进程。

---

### 8. `kcp::Server::run()` 的 on_init 失败不立即 release

**位置**: [src/kcp/server.cpp:37-43](src/kcp/server.cpp#L37-L43)

```cpp
init();
int err = event_->on_init(this);
if (err) {
    state_.store(core::State::Stopped);
    return;                          // ← epfd_, stop_evfd_ 留着,直到 dtor
}
```

dtor 兜底但 server 寿命可能很长。卫生问题，不算 bug。

**修法**: 失败路径补一个 `release();`。

---

### 9. IEvent 没注明「会被多线程并发调用」

**位置**: [include/kcp/server.hpp:29-62](include/kcp/server.hpp#L29-L62)

`typhon::Server` 创建 N 个 `kcp::Server` 时共享同一个 IEvent 实例 → `on_init` / `on_connected` / `on_data` 等回调会被 **N 个线程并发触发**。用户自己实现时极易踩坑（写一个 unordered_map 不加锁就坏了）。

**修法**: 在 IEvent 类上加 doxygen warning，明确「所有回调可能从多个线程并发触发，实现需自保线程安全」。

---

## 可以做的优化（不是 bug）

### 10. KCP `output` 不必 `shared_from_this`

**位置**: [src/kcp/server.cpp:88](src/kcp/server.cpp#L88)

现在 SndBuf 已经存 sockaddr 快照（不依赖 Session 生命周期），output 调用栈上 session 是 KCP 的 user 指针，KCP 内部不会在 output 期间销毁 session。直接 `auto* s = (Session*)user;` 省一次原子 incr/decr。

### 11. `SndBuf` ctor 形参 `buf` 跟成员 `buf` 同名

**位置**: [include/core/buffer.hpp:25-32](include/core/buffer.hpp#L25-L32)

形参 `buf` 跟成员 `buf` 同名，靠 `this->` 消歧。改名 `data` 之类。

### 12. `RcvBuf::buf` 是堆指针，`SndBuf::buf` 是 inline 数组 —— 同名不同物

跨结构体看代码容易愣一下。命名分开（比如 SndBuf 用 `data[UDP_MTU]`）心智负担更轻。

### 13. `tcp::Session::sbuf_` 没 reserve

**位置**: [include/tcp/session.hpp:136](include/tcp/session.hpp#L136)

`std::vector<uint8_t> sbuf_ {}` 默认 0 容量，第一次 insert 触发 alloc，grow 时 realloc + memcpy。等「sbuf_ 是热路径」实测确认后再考虑 reserve。

### 14. `sending_.store(false)` 默认 seq_cst，relaxed 即可

**位置**: [src/tcp/worker.cpp:144](src/tcp/worker.cpp#L144)

SPSC 自己有 release/acquire 配对保证数据可见性，`sending_` 只用来 wake-coalesce，relaxed 足够。

### 15. CPU affinity

PLAN 已挂着。网关 worker 绑核能稳定 tail latency，应当尽早做。

---

## 已修复 / 确认无问题（参考）

- ~~`Worker::push` 静默丢事件 + 泄漏~~ → 改成 `ASSERT(rque_.enqueue(...))`，硬失败而非泄漏。
- ~~KCP `add_session` 拒绝时误触发 `on_disconnected`~~ → 改成 `sessions_.erase(conv)`。
- ~~`on_qe_recv_handle` 用 ASSERT 处理「session 不存在」~~ → 改成 `if (sess == nullptr) { mi_free(rbuf); return 0; }`。
- ~~ASSERT 宏内双 abort~~ → 删掉第二个 abort。
- ~~`tcp_listen` 漏调 `::listen()`~~ → 在 `Server::run()` 里补上。
- ~~`Session::send` 就地 byte-swap `pk`~~ → 当前用法（handler 内 decode→send→丢弃）安全；在 `send` 上方加了 warning 注释明确契约。
- ~~`tnow_` 跨线程裸读~~ → 改成 `std::atomic<uint32_t>` + `memory_order_relaxed`。
- ~~`regist_handler` 无线程约束~~ → 加了 doxygen warning，明确「必须在 `run()` 之前调用」。
- ~~SIGINT 信号 handler 通往 spdlog 路径~~ → 确认无问题。`eventfd` write 在 EFD_NONBLOCK 下实际不会失败（仅 EAGAIN/EBADF），write 失败意味着真实 bug；此时 ASSERT 抓出来比静默 hang 强。
- ~~KCP `output` 回调里 `shared_from_this()` 没保护~~ → 确认无问题。output 只可能在 `update()`（栈上有 `s = itr->second`）或用户 handler（栈上有 `kcp = get_session(conv)`）触发；`~Session()` 里 `ikcp_release` 只 free 内存不回调。两条路径都有活 shared_ptr，`shared_from_this()` 一定成功。
- ~~`PkgBuf::decode` PackageTail 偏移错误~~ → 改成 `buf + rpos + pklen - PKG_TAIL_LEN`，配合 test_tcp.py 模拟 gateway stamp tail。
- ~~`tcp::Session::sbuf_` 无上限~~ → 确认无问题。TCP 仅 backend 内网通信，不是攻击面。
- ~~`kcp::Server::sque_` 无上限~~ → SendBuf 加 `time` 字段；`update()` 在 sendmmsg 之前先扫一遍把过期段 erase；`SendBufQue` 改为 `std::deque`，前端 erase O(1)。
- ~~`mi_malloc` 返回 nullptr 不检查~~ → 三处分配点都加了 ASSERT 早 abort。
- ~~`Session::Ptr = shared_ptr` 但 `create()` 返回 `make_unique`~~ → 改成 `std::make_shared<Session>(...)`。
- ~~KCP `recv_pk` 与 TCP `recv` 对「重复 idem」返回值不一致~~ → KCP 也改成 duplicate 返回 `0`，跟 TCP 对齐。
- ~~`kcp::Server::on_udp_handle` 无坏包节流~~ → `recv_pk` 返回 `< -1` 时 `on_udp_handle` 直接 `remove_session(conv)`，恶意 conv 灌坏包会立刻被踢掉。
- ~~`kcp::Server::update` 的 `sque_.erase` O(N)~~ → 跟随 `sque_` 改 deque 一并解决。
- ~~`Session::buf[PKG_MAX_LEN]` 占用 64KB / 连接~~ → `RcvBuf` 改成 lazy-heap：`buf` 初始 nullptr，首次 `append()` 才 `mi_malloc(PKG_MAX_LEN)`。idle / 半挂 session 完全不占堆；active session 占 64KB（同原版）。`cap` / `inbuf` / `upgrade()` 全删，结构体从 ~4KB 缩到 16B。
- ~~`SndBuf::kcp` 持 raw `Session*` 导致 UAF~~ → SndBuf 改成存 `sockaddr_storage` + `socklen_t` 快照，与 Session 生命周期完全解耦。`void* kcp` 字段删除；sendmmsg loop 直接用 `&buf->addr`。
- ~~`SndBuf` ctor 缺 `len <= UDP_MTU` 校验，超长悄悄发垃圾~~ → ctor 加 `ASSERT(len <= UDP_MTU)`，配合 KCP `ikcp_setmtu` 不变式。
- ~~`SndBuf` 的 `sque_` 用 raw 指针，erase 不 delete 泄漏~~ → 改成 `std::deque<std::unique_ptr<SndBuf>>`，emplace 用 `make_unique` exception-safe。
- ~~core 反向 include kcp/server.hpp（分层污染）~~ → SndBuf ctor 接 `uint32_t time` 参数，core 层不再依赖 kcp。`src/core/buffer.cpp` 已空，可以从 Makefile 删除。
- ~~`RcvBuf::decode` 缺 `pklen >= HEADER + TAIL` 校验~~ → 加了 ASSERT；同时保留 `readable() >= PKG_HEADER_LEN` 前置检查（防 TCP 部分到达时读出垃圾 pklen 误触发 ASSERT）。
- ~~`RcvBuf` compact 时机~~ → 在 `decode` 入口加 `if (rpos > cap/2) compact();` lazy compact，把开销分摊，避免 append 在 wpos 顶满才做大块 memmove。
- ~~Handler 槽 `[UINT16_MAX + 1]` 占用 256KB~~ → 缩成 `[MAX_HANDLERS = 1024]`（4KB / Server）；`get_handler` 越界返 nullptr，`regist_handler` 越界 ASSERT，保证脏 pk_id 不会 OOB 读 / 程序员漏写不会 OOB 踩内存。
- ~~EPOLLOUT 注册策略~~ → 维持现状不改。常驻 EPOLLOUT|ET 在 accept 时多触发一次 Send 事件 drain 空 sbuf_，代价比每次 EPOLL_CTL_MOD 更小。
