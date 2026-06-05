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

### 2. `tcp::Session::recv` 的 idem 校验对「gateway 多路复用」架构不对

**位置**: [include/tcp/session.hpp](include/tcp/session.hpp)

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

### 6. 转发路径的内嵌字节序 —— `Pke` 递归翻 vs KCP 透传来的 net 序

**位置**: [include/core/package.hpp](include/core/package.hpp)（`Pke<O>` 的 `hton`/`ntoh` 递归翻外层+内嵌）

字节序已全栈 phantom 化(见 memory `project-typhon-pke-send-byteorder`)：`Pke` 的 `hton`/`ntoh` **递归翻外层+内嵌**(即原 A 方案被固化)，`Pke<Host>` 的语义是"外层+内嵌**整体** host 序"。对「**本地构造**」的包正确：
- 心跳（`Connector::update` 构造 `Pke<Host>`）✅
- 回程（`Session::send` 后端 host 序构造）✅

但「**转发**」是个**混合态**，不符合 `Pke<Host>` 的前提：网关 `on_data` 把客户端包转后端时，内嵌 Package 是 KCP 透传来的、**本来就是网络序**，而新 prepend 的 PackageEx 外层头是 host 序 —— 这是"外 host / 内 net"，**不能直接塞进 `Pke<Host>` 然后 `hton`**（会把已经是 net 的内嵌又翻回去）。转发又是最高频路径。

现在没爆，是因为 `on_data` 转发还没实现。**接转发时必须给这条混合态单独的路**，候选：
- 转发入口只翻外层 PackageEx 头(`htons/htonl` 三个字段)，内嵌原样不动 —— 需要一个绕过 `Pke` 递归 `hton` 的"只翻外层"发送路径；
- 或转发前先对内嵌 `pk_ntoh` 抵消，让它能走统一的 `Pke<Host> → hton`（多两次 byteswap，热路径上不划算）。

**接 `on_data` 转发时定**，倾向第一个(热路径不做多余翻转)。

---

## 可以做的优化（不是 bug）

### 3. `RcvBuf::buf` 是堆指针，`SndBuf::buf` 是 inline 数组 —— 同名不同物

跨结构体看代码容易愣一下。命名分开（比如 SndBuf 用 `data[UDP_MTU]`）心智负担更轻。低优先级，目前不动。

### 4. `tcp::Session::sbuf_` 没 reserve

**位置**: [include/tcp/session.hpp](include/tcp/session.hpp)

`std::vector<uint8_t> sbuf_ {}` 默认 0 容量，第一次 insert 触发 alloc，grow 时 realloc + memcpy。等「sbuf_ 是热路径」实测确认后再考虑 reserve。

### 5. CPU affinity

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
- ~~KCP `output` 回调里 `shared_from_this()` 没保护~~ → 确认无问题。output 只可能在 `update()`（栈上有 `s = itr->second`）或用户 handler（栈上有 `kcp = get_session(conv)`）触发；`~Session()` 里 `ikcp_release` 只 free 内存不回调。
- ~~`PkgBuf::decode` PackageTail 偏移错误~~ → 改成 `buf + rpos + pklen - PKG_TAIL_LEN`，配合 test_tcp.py 模拟 gateway stamp tail。
- ~~`tcp::Session::sbuf_` 无上限~~ → 确认无问题。TCP 仅 backend 内网通信，不是攻击面。
- ~~`kcp::Server::sque_` 无上限~~ → SendBuf 加 `time` 字段；`update()` 在 sendmmsg 之前先扫一遍把过期段 erase。
- ~~`mi_malloc` 返回 nullptr 不检查~~ → 三处分配点都加了 ASSERT 早 abort。
- ~~`Session::Ptr = shared_ptr` 但 `create()` 返回 `make_unique`~~ → 改成 `std::make_shared<Session>(...)`。
- ~~KCP `recv_pk` 与 TCP `recv` 对「重复 idem」返回值不一致~~ → KCP 也改成 duplicate 返回 `0`，跟 TCP 对齐。
- ~~`kcp::Server::on_udp_handle` 无坏包节流~~ → `recv_pk` 返回 `< -1` 时 `on_udp_handle` 直接 `remove_session(conv)`，恶意 conv 灌坏包会立刻被踢掉。
- ~~`kcp::Server::update` 的 `sque_.erase` O(N)~~ → 跟随 `sque_` 改 deque 一并解决。
- ~~`Session::buf[PKG_MAX_LEN]` 占用 64KB / 连接~~ → `RcvBuf` 改成 lazy-heap：`buf` 初始 nullptr，首次 `append()` 才 `mi_malloc(PKG_MAX_LEN)`。idle / 半挂 session 完全不占堆；active session 占 64KB（同原版）。
- ~~`SndBuf::kcp` 持 raw `Session*` 导致 UAF~~ → SndBuf 改成存 `sockaddr_storage` + `socklen_t` 快照，与 Session 生命周期完全解耦。
- ~~`SndBuf` ctor 缺 `len <= UDP_MTU` 校验~~ → ctor 加 `ASSERT(len <= UDP_MTU)`。
- ~~`SndBuf` 的 `sque_` 用 raw 指针，erase 不 delete 泄漏~~ → 后续接 ObjPool 统一管理，acquire / release 配 mi_malloc / mi_free。
- ~~core 反向 include kcp/server.hpp（分层污染）~~ → SndBuf ctor 接 `uint32_t time` 参数，core 层不再依赖 kcp。
- ~~`RcvBuf::decode` 缺 `pklen >= HEADER + TAIL` 校验~~ → 加了 ASSERT；同时保留 `readable() >= PKG_HEADER_LEN` 前置检查。
- ~~`RcvBuf` compact 时机~~ → `decode` 入口加 `if (rpos > cap/2) compact();` lazy compact。
- ~~Handler 槽 `[UINT16_MAX + 1]` 占用 256KB~~ → 缩成 `[MAX_HANDLERS = 1024]`；`get_handler` 越界返 nullptr，`regist_handler` 越界 ASSERT。
- ~~EPOLLOUT 注册策略~~ → 维持现状不改。
- ~~IEvent 没注明「会被多线程并发调用」~~ → kcp::Server::IEvent 和 tcp::Server::IEvent 都加了 doxygen warning。

### 这一轮收尾时一并清掉的（来自最近的全量审查）

- ~~`tcp::Server::run()` on_init 失败让进程 `std::terminate`~~ → `release()` 内部改成「先 stop+join worker，再 close fd」的顺序，所有调用 path（on_init 失败 / 正常退出 / ~Server）都 idempotent 且安全。
- ~~`tcp::Session::send` 大包尺寸校验上限错了~~ → 直接删掉错误的 `> PKG_MAX_LEN - PKG_TAIL_LEN` 检查；pk_len 是 uint16_t 天然有界。
- ~~pidfile 不 truncate，PID 变短残留旧字节~~ → 拿锁后 `ASSERT(::ftruncate(fd, 0) == 0)`。
- ~~KCP `add_session` 拒绝时栈上还在用 `s` 处理本次包~~ → `add_session` 返回 int，caller 见非 0 立即 `continue` 跳过本次包。
- ~~`typhon::Server::run()` 失败路径不 clear `servers_`~~ → 中间步骤改 ASSERT，失败立即 abort，进程死掉就不存在脏状态重启。
- ~~`kcp::Server::run()` on_init 失败不立即 release~~ → 失败 path 补 `release()`。
- ~~KCP `output` 不必 `shared_from_this`~~ → 改成 `auto* s = (Session*)user;`，省一次原子。
- ~~`SndBuf` ctor 形参 `buf` 跟成员 `buf` 同名~~ → 形参改名 `b` / `l`。
- ~~`sending_.store(false)` 默认 seq_cst~~ → 改成 `memory_order_relaxed`。
- ~~`RcvBuf` 升级单向不可逆 / 4KB inline 浪费~~ → 直接砍掉 inbuf，改纯 lazy-heap（buf 初始 nullptr，首次 append 才 mi_malloc PKG_MAX_LEN）。
- ~~ObjPool 析构 `::operator delete` 不匹配 `mi_malloc`~~ → 改成 `mi_free`，allocator 配对。
- ~~ObjPool acquire OOM 时 placement-new 写 nullptr~~ → `ASSERT(obj != nullptr, "mi_malloc failed")`。
- ~~`tnow() - timeout/2` 启动头 15s underflow~~ → 改回加法形式 `buf->time + timeout < tnow()`。
- ~~`kcp::Server` 析构 `sque_` 残留泄漏~~ → ~Server 里循环 `mi_free` 释放 sque_ 残留。
- ~~`recvmmsg` 循环无上限可饿死 update~~ → 加 `MAX_ROUND = 8` 限轮数。
- ~~`recvmmsg` 不检查 MSG_TRUNC~~ → 检测到截断包打 WARN 后 `continue` 直接跳过。
- ~~kcp::Server::run() stop 后不可重启（sessions_/sque_/tnow_ 不重置）~~ → release() 末尾清理 sessions_、归还 sque_ 到 pool、`tnow_ = 0`。
- ~~kcp::Session::recv_pk 缺 pk_id / pk_dst_id 校验~~ → 补 `pk_id ∈ [1, MAX_HANDLERS)` / `pk_idem > 0` / `pk_dst_id > 0` 三道校验，对应返回码 -5 / -6 / -7。
- ~~`MAX_HANDLERS` magic number~~ → 提到 `core/package.hpp` 作为协议级常量，tcp/kcp 两边共用。
- ~~recv_pk doc 跟实现不一致~~ → doc 列全 -7 到 1 共 8 个返回码并加合法值约定。
- ~~package.hpp 顶部缺字段合法值约定~~ → 加「字段合法值约定」段，说明 pk_id / pk_idem / pk_dst_id 的合法范围与违反时的服务端返回码。
