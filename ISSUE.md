# typhon 待修问题清单

代码审查后尚未修复的问题。优先级从上到下递减。

---

## P2 — 资源 / 容量

### 1. KCP 无 conv 鉴权 → DDoS 矢量

**位置**: [src/kcp/server.cpp:194-197](src/kcp/server.cpp#L194-L197)

- 任意陌生 conv 直接 `Session::create` 加进 sessions_。
- 30 s timeout 才清，攻击者可在 30 s 内灌爆 sessions_ map。
- 必须加 conv 频率限制 / pre-auth。属于 Phase 1 跑通后的强需求。

---

## 可以做的优化（不是 bug）

### 2. CPU affinity

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
