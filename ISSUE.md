# typhon 待修问题清单

代码审查后尚未修复的问题。优先级从上到下递减。

---

## P0 — 数据正确性

### 1. `PkgBuf::decode` 的 PackageTail 偏移错误

**位置**: [include/tcp/session.hpp:54-72](include/tcp/session.hpp#L54-L72)

```cpp
if (readable() < core::PKG_HEADER_LEN + core::PKG_TAIL_LEN) {
    return false;
}
...
auto* t = (core::PackageTail*)(buf + rpos + pklen);
pkt_ntoh(t);
```

**问题**:
- 协议约定「网关→后端」方向 tail **含在** `pk_len` 内（见 [include/core/package.hpp:25-29](include/core/package.hpp#L25)），tail 应该位于 `buf + rpos + pklen - PKG_TAIL_LEN`，**不是** `+ pklen`。
- 现在每次 decode 都会就地 byte-swap 包尾后 8 字节。若 pbuf_ 中同时驻留下一个包（一次 recv 进来多个，或上一个未消费完又来新包），就会**就地损坏下一个包的前 8 字节**（pk_len / pk_id / pk_idem 全部错乱）。
- echo 测试 20 Hz × 5 client 凑不齐共驻，所以表面跑得通；rate 一上来或者内核 batch 多个包到一次 recv 就立刻挂。
- 另外前置 `readable() < HEADER + TAIL` 也不对 ——「client→gateway」方向 pk_len 不含 tail，按 20 字节阈值会延迟解一个 12 字节的小包。

**修复方向**:
- 要么 decoder 配置/参数指明「是否含 tail」，两条 path（gateway / 直连）走不同 decode。
- 要么 echo 路径下根本不读 `PackageTail`（pkt 传 nullptr 给 handler）。

---

### 2. `Session::send` 就地 byte-swap 入参 `pk`（API 陷阱）

**位置**: [src/tcp/session.cpp:24-25](src/tcp/session.cpp#L24-L25)

```cpp
uint8_t* buf = (uint8_t*)pk;
core::pk_hton(pk);
```

**问题**:
- 调用方再访问 `pk` 字段就是网络字节序了。
- 同一个 `pk` send 两次（广播 / 重试），第二次再 `pk_hton` 把字节翻回来 → 飞行中半数据损坏。
- echo 路径正好 send 后不再 touch pk，所以工作；这是一个 API 隐式契约。

**修复方向**:
- 头文件注释明确「pk 在 send 之后内容不再可用」。
- 或者 send 内部拷贝到临时缓冲。

---

## P1 — 并发 / 时序

### 3. `tnow_` 跨线程裸读（data race）

**位置**: [include/tcp/server.hpp:179](include/tcp/server.hpp#L179)

```cpp
uint32_t tnow_ { 0 };
```

- 主线程在 epoll 循环里写，所有 worker 通过 `server_->tnow()` 读。
- 严格 C++ 标准下是 UB；x86 上 32-bit aligned load/store 在硬件层原子，实际不出问题，但属于「碰运气」。

**修复**: 改成 `std::atomic<uint32_t>` + relaxed。

---

### 4. `regist_handler` 没有线程安全约束

**位置**: [include/tcp/server.hpp:146-152](include/tcp/server.hpp#L146-L152)

- worker 通过 `server_->get_handler(pkid)` 读 `handlers[]`，主线程写。
- 目前所有调用都在 `run()` 之前注册 → 跑起来之后只读，OK，但**没有文档约束**。

**修复**: 注释明确「仅允许在 `run()` 之前注册」。

---

## P2 — 资源 / 容量

### 7. `Session::sbuf_` 与 `kcp::Server::sque_` 无上限

**位置**:
- [include/tcp/session.hpp:192](include/tcp/session.hpp#L192) `std::vector<uint8_t> sbuf_`
- [include/kcp/server.hpp:216](include/kcp/server.hpp#L216) `SendBufQue sque_`

- TCP outbound 缓冲不停 insert，慢客户端 / 半挂连接会让进程内存爆。
- KCP `sque_` 同样，sendmmsg 跟不上就堆积。
- 至少加一个上限阈值，超过就主动断该 session。

---

### 8. `mi_malloc` 返回 nullptr 不检查

**位置**:
- [src/tcp/server.cpp:220](src/tcp/server.cpp#L220) `RcvBuf* rbuf = (RcvBuf*)::mi_malloc(...)`
- [include/kcp/server.hpp:51](include/kcp/server.hpp#L51) `SendBuf::SendBuf` 里 `mi_malloc`
- [src/kcp/server.cpp:20](src/kcp/server.cpp#L20) `riovecs_[i].iov_base = ::mi_malloc(...)`

- 全部直接 memcpy。OOM 立即 segfault。
- 至少加 ASSERT 早 abort，让现场可定位。

---

### 9. `Session::buf[PKG_MAX_LEN]` 占用 64KB / 连接

**位置**: [include/tcp/session.hpp:22](include/tcp/session.hpp#L22)

- `MAX_CONN = 8192` → 全连满约 512MB pbuf_。
- 不是 bug，是容量决策。如果实际包远小于 64KB（MMO 量级 800B），inline buf 可以做小（4KB / 8KB），用 secondary heap-buffer 处理超大包。

---

### 10. KCP 无 conv 鉴权 → DDoS 矢量

**位置**: [src/kcp/server.cpp:194-197](src/kcp/server.cpp#L194-L197)

- 任意陌生 conv 直接 `Session::create` 加进 sessions_。
- 30 s timeout 才清，攻击者可在 30 s 内灌爆 sessions_ map。
- 必须加 conv 频率限制 / pre-auth。属于 Phase 1 跑通后的强需求。

---

### 11. `Session::Ptr = shared_ptr` 但 `create()` 返回 `make_unique`

**位置**: [include/kcp/session.hpp:35-38](include/kcp/session.hpp#L35-L38)

```cpp
typedef std::shared_ptr<Session> Ptr;

static Ptr create(...) noexcept {
    return std::make_unique<Session>(...);  // unique_ptr → shared_ptr 隐式转换
}
```

- 能编过，行为正确。
- 但写成 `std::make_shared<Session>(...)` 更直接，少一次堆分配（合并 control block 与对象）。

---

## P3 — 小问题 / 风格

### 12. KCP `recv_pk` 和 TCP `recv` 对「重复 idem」返回值不一致

- KCP: `rcv_idem_ >= pk_idem` → 返回 `-1`（非法包） [include/kcp/session.hpp:232-234](include/kcp/session.hpp#L232-L234)
- TCP: `rcv_idem_ >= pk_idem` → 返回 `0`（skip） [include/tcp/session.hpp:166-168](include/tcp/session.hpp#L166-L168)

两端语义应该对齐，省得业务层踩坑。

---

### 13. `kcp::Server::on_udp_handle` 无坏包节流

**位置**: [src/kcp/server.cpp:204-219](src/kcp/server.cpp#L204-L219)

- `recv_pk` 返回 -1 用 `continue`，逻辑正确（跳过坏包拿下一条）。
- 但没有任何节流：恶意 conv 持续灌坏包能让 worker 一直在 recv 循环里转。
- 加一个 per-conv 坏包计数器，超阈值直接 remove_session。

---

### 14. `kcp::Server::update` 的 `sque_.erase(begin, begin + nsnd)` O(N)

**位置**: [src/kcp/server.cpp:275](src/kcp/server.cpp#L275)

- 单次更新可能 erase 几百项，每次都从前面 erase 是 O(N) 平移。
- 改成滑动指针 + 周期 compact，或用 `std::deque`。

---

## 可以做的优化（不是 bug）

### 15. EPOLLOUT 注册策略

**位置**: [src/tcp/server.cpp:183](src/tcp/server.cpp#L183) `event.events = EPOLLIN | EPOLLET | EPOLLOUT;`

- 每个 cfd 都常驻 EPOLLOUT|ET，刚 accept 就会触发一次 Send 事件去 drain 空 sbuf_，无谓负担。
- 改成「只在 sbuf_ 第一次堆积时 `EPOLL_CTL_MOD` 加 EPOLLOUT，drain 空后 MOD 去掉」可以省掉 spurious wakeup。代价：每次都要 mod，要权衡。

### 16. `PkgBuf` compact 时机

- 现在只在 `append` 失败才 compact，最坏情况会等到 wpos 顶到 PKG_MAX_LEN 才做大块 memmove。
- 可以在 `decode` 之后 `rpos > N/2` 时 lazy compact，把代价分摊。

### 17. KCP `SendBuf` 内存池化

- KCP 每个 segment 一次 `mi_malloc`，频繁分配。
- 对 ≤ MTU 的固定大小段做一个小尺寸 free-list 池。

### 18. Handler 槽换 `std::array` / 分桶

**位置**: [include/tcp/server.hpp:186](include/tcp/server.hpp#L186)

- `handlers[UINT16_MAX + 1]` = 256KB / Server 实例（指针数组）。
- 如果 pk_id 实际只用低位（< 1024 个业务号），可以分桶减小占用。

### 19. CPU affinity

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
- ~~SIGINT 信号 handler 通往 spdlog 路径~~ → 确认无问题。`eventfd` write 在 EFD_NONBLOCK 下实际不会失败（仅 EAGAIN/EBADF），write 失败意味着真实 bug；此时 ASSERT 抓出来比静默 hang 强。理论上 spdlog 在信号上下文是 async-signal-unsafe，但前提概率极低。
- ~~KCP `output` 回调里 `shared_from_this()` 没保护~~ → 确认无问题。output 只可能在 `update()`（栈上有 `s = itr->second`）或用户 handler（栈上有 `kcp = get_session(conv)`）触发；`~Session()` 里 `ikcp_release` 只 free 内存不回调。两条路径都有活 shared_ptr，`shared_from_this()` 一定成功。
