# `tcp::Server` — 后端业务服骨架

> 后端 instance 的 TCP 服务端。**只接网关连接**(不暴露给客户端,不存在远程攻击者 → 协议层 `ASSERT`-on-malformed 是合理的 fail-fast)。
> 主线程只 `accept`,N 个 `Reactor` 各持一个 epoll 跑自己的连接;同一 `fd` 始终落同一 Reactor → **session 不跨线程,无锁**。
> 源码:[include/tcp/server.hpp](../Adam/include/tcp/server.hpp) · [include/tcp/reactor.hpp](../Adam/include/tcp/reactor.hpp) · [src/tcp/reactor.cpp](../Adam/src/tcp/reactor.cpp)

---

## 1. 线程划分

```
主线程 (Server::run)                     Reactor 0 (独立线程)
┌──────────────────────┐                ┌────────────────────────────────┐
│ epoll:               │                │ epoll: evfd + 自己名下所有连接 fd │
│   evfd_ (停止)        │  Message       │                                │
│   lfd_  (监听)        │ ──────────────►│ MPSC 队列 + eventfd 唤醒         │
│                      │ SessionConnected│ sesss_ (按 fd) / ters_ (按 uid) │
│ accept → 轮询派发     │                │ 收字节 → 切包 → 派发 handler     │
└──────────────────────┘                └────────────────────────────────┘
                        │                Reactor 1 …  Reactor N-1
                        └───────────────►
```

**职责切分:**
- **主线程**:epoll 里**只挂 `evfd_`(停止)和 `lfd_`(监听)**,`accept` 到新连接后发一条 `SessionConnected` 消息给某个 Reactor,自己不碰任何连接 fd、不读一个字节。
- **Reactor(每线程数据面)**:把拿到的 fd 注册进**自己的** epoll(ET),之后这条连接的读、写、切包、派发、超时清理**全在这一条线程上**。`sesss_` / `ters_` 只被本 Reactor 读写。

> 连接按 `acc_next_++ % reactors_.size()` **轮询**分配,不是 `fd % N`。fd 的数值分布不均(复用、其他子系统占用),轮询更稳。

---

## 2. 主线程 → Reactor:MPSC 队列 + eventfd

```cpp
void notify(Message* m) {
    ASSERT(mque_.enqueue(std::move(m)), "MPSC 队列已满, 请对队列扩容");
    bool expected = false;
    if (mq_workering_.compare_exchange_strong(expected, true)) {
        ::write(mfd_, &event, sizeof(event));   // 只在"从空变非空"时才写 eventfd
    }
}
```

- 队列是 **有界 lockfree MPSC**([utils/mpsc.hpp](../Adam/include/utils/mpsc.hpp)),不是 SPSC —— 除了主线程,别的 Reactor 也会往这里投(跨 Reactor 的顶号仲裁)。
- `mq_workering_` 的 CAS 是 **park-unpark 双检**:队列从空变非空时才真正 `write(eventfd)`,连续投递不会每条都触发一次 syscall。
- **队列满时 `ASSERT`** —— 当前是 fail-fast,靠扩容解决,没做背压。

`Message::Type` 现有七种:`Stop` / `SessionConnected` / `TerminalEnter` / `TerminalLeave` / `TerminalHandle` / `TerminalKick` / `MidHandle`。见 [include/tcp/message.hpp](../Adam/include/tcp/message.hpp)。

---

## 3. 收包 → 业务派发

Reactor 从自己的 epoll 拿到 `EPOLLIN` → 读进 `Session` 的 `RcvBuf` → `frame_decode` 切包 → 查表派发。

**两套注册表,按用途分:**

| 注册 | 键 | handler 签名 | 用途 |
|---|---|---|---|
| `regist_handler(pid, h)` | `uint16_t` PID | `void(Context&, Package*)` | 裸包 —— 拿到的是解好的 `Package` |
| `regist_handler(mid, h)` | `uint16_t` MID | `void(Message*)` | 消息 —— 走 `MidHandle`,跨 Reactor 投递用 |

- 两张表都是 `absl::flat_hash_map`,**必须在 `run()` 之前注册完** —— 运行期 Reactor 只读,没有任何同步。重复注册同一个 pid/mid 会被拒绝。
- `Context { Reactor* reactor; Terminal* terminal; }` 是 handler 的上下文,`ctx.terminate(code)` 踢掉当前终端。
- `frame_decode` **自带分帧**,半包返回 `0`。切包循环必须判 `< 0` 才算错误 —— 细节见 [package.md](package.md#3-codec四个函数)。

---

## 4. 框架内建的 PID

这些在 `on_package_handle` 之前就被 Reactor 截住,不会落到业务 handler:

- `on_ping` —— PING/PONG 心跳
- `on_serv_regist` —— 网关连上来时的注册握手
- `on_terminal_enter_req` / `on_terminal_leave_req` —— 终端进入 / 离开本后端
- `on_terminal_offline_notify` —— 网关告知终端已下线

业务只需要关心 `PID_CUSTOM(200)` 以上的号段。

---

## 5. 终端与顶号

- **`Terminal`(按 uid)**:`ters_` 存在 Reactor 上,代表"某个用户在本后端的实体"。它与 `Session`(按 fd,代表一条网关连接)**生命周期分离** —— 一条网关连接上跑着很多终端。
- **`Directory`(在 `Server` 上)**:跨 Reactor 的顶号仲裁。同一个 uid 可能先后落到不同 Reactor,由 Directory 定位旧的那个并投 `TerminalKick` 过去,**不加锁,走消息队列**。
- `kick_terminal(uid, code)` 是对外入口,`code` 会一路带到客户端。

---

## 6. session 生命周期与已知点

- **容器**:`sesss_`(`flat_hash_map<fd, Session::Ptr>`),`shared_ptr` 让业务可以跨调用安全持有,不会 UAF。
- **超时**:Reactor 每秒 `check_timeout` 扫自己的 session,`last_recv_ms` 超时则 `remove_session`。因为连接不再跨线程分片,不需要旧版那种 `i = id_; i += ws` 的切片扫描。
- **新连接首读**:`on_session_connected` 注册进 epoll(ET)之后会**立刻补一次读** —— ET 模式下,注册之前就已到达的数据不会再触发事件,不补读会挂死。
- **release 顺序**:`reactors_.clear()` 必须在 `threads_.join()` 之后,否则 Reactor 线程持有的 `this` 变 UAF。
