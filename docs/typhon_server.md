# 网关进程编排 — `kcp::Server` 控制面

> 整个网关进程的入口。负责:**装载 BPF(XDP + sk_reuseport)** → 起 **N 个 [`kcp::Worker`](kcp_server.md)** → 跑**控制面 loop**(服务发现 + Worker 回流)→ 向 etcd 注册。
> 主线程兼控制线程,**不碰数据面** —— 数据面在 N 个 Worker 里。
> 源码:[include/kcp/server.hpp](../Adam/include/kcp/server.hpp) · [src/kcp/server.cpp](../Adam/src/kcp/server.cpp)

> **命名沿革**:这一层曾经是独立的 `typhon::Server`(`include/typhon.hpp`)。重构后它并进了 `kcp::Server`,而原来那个每线程的 `kcp::Server` 改名 `kcp::Worker`。本文档讲的是**控制面**,数据面见 [kcp_server.md](kcp_server.md)。

---

## 1. 进程结构

```
                  ┌─────────────────────── kcp::Server (主线程) ──────────────────────┐
                  │  EnvelopeFilter (XDP)   Router (sk_reuseport)   etcd 注册/续租     │
                  │  epoll: evfd_ 一个 fd    evque_ (MPSC)  handlers_ (PID→handler)    │
                  └───┬──────────────────────────────────────────────────▲────────────┘
      Message + eventfd│ (AddServ / RmvServ / Stop)         Event + eventfd│ (后端掉线回流)
                  ┌───▼──────────┬──────────────┬──────────────────────┴────────────┐
                  │  Worker 0    │  Worker 1    │  …  Worker N-1                    │
                  │  UDP socket(SO_REUSEPORT) + epoll + sesss_ + servs_ + 发送队列    │
                  └──────────────┴──────────────┴───────────────────────────────────┘
                         ▲ sk_reuseport 按 conv % N 分流
```

Worker 数默认取 ≈ 核数 − 1。`nthreads` 会写进 etcd 的 `ServerInfo`,Eva 生成 conv 时要读它来满足 `conv % N == user_id % N` 不变量。

---

## 2. 启动序列(**顺序敏感**)

```
EnvelopeFilter attach (XDP)  →  socket bind  →  Router attach (sk_reuseport)  →  起 Worker 线程  →  etcd 注册
```

XDP 必须先于 socket bind 生效 —— **开机即受保护**,启动期被攻击也挡得住。反过来的话,attach 之前那个窗口里伪造包会直接进 socket 队列。

**停机顺序刚好相反**:`stop()` 把 `state_` 翻 `Stopping` → 给每个 Worker `notify(Stop)` 唤醒它的 epoll → Worker drain 掉队列里的 Stop 后循环自然退出 → `join` → 卸 BPF。

> `workers_.clear()` 必须在 `threads_` join 之后,否则 Worker 线程持有的 `this` 变 UAF。

---

## 3. 控制面 loop + 双向事件通道

主线程 `epoll_wait(timeout = 10s)`:**超时(`n == 0`)** 跑周期服务发现,**有事件(`n > 0`)** 处理 Worker 回流。

| 方向 | 通道 | 载荷 |
|---|---|---|
| 主线程 → Worker | `Worker::mque_` —— 有界 lockfree MPSC + `evfd_` | `Message`:`AddServ` / `RmvServ` / `Stop` … |
| Worker → 主线程 | `Server::evque_` —— 有界 lockfree MPSC + `evfd_` | `Event`:后端掉线等控制面事件 |

**两个方向都是 MPSC,不是 SPSC** —— 主线程→Worker 这条也可能被别的 Worker 写(跨 Worker 的 s2c 转发),Worker→主线程这条更是 N 个生产者。

两端都用同一套 **park-unpark 双检**:

```cpp
mque_.enqueue(m);
bool expected = false;
if (mq_workring_.compare_exchange_strong(expected, true)) {
    ::write(evfd_, &event, sizeof(event));   // 只在"由空变非空"时才捅一次
}
```

连续投递不会每条都触发一次 `write` syscall。消费侧的**两次 drain 与 `seq_cst` 是刻意写法** —— 清标志与再检查之间有窗口,少一次 drain 就会丢唤醒,别当冗余优化掉。

> 队列满时 `ASSERT` —— 目前是 fail-fast,靠扩容解决,没做背压。

---

## 4. 服务发现 + 重连(`update_serv`)

- 网关向 etcd 注册 `/{name}/{id:08X}`(如 `/moses/000003E8`),value 是 `ServerInfo` JSON(含 `nthreads`、`router`、受理的 PID 集合),TTL 续租。
- 后端以 `name = "public/<type>"` 注册。网关读 **`/public` 前缀**发现所有后端 → 广播 `AddServ` 给每个 Worker → 各 Worker 各自建自己的 `Connector`。
- 某个 Worker 判定后端死了 → 摘除 + 经 `evque_` 回流 → 主线程记下 → **下一轮** `update_serv` 重新广播。

**重连风暴防护**:`update_serv` **只在 epoll 超时(`n == 0`)分支跑**,不被掉线事件即时驱动 —— 相当于给重连一个 10s 固定退避。否则"断开 → 回流 → 立即重连 → 又断"会把主线程钉成 busy-loop。

> **不做 Connector 层 TCP 自重连**(刻意):掉线即摘,重连统一由服务发现层驱动 —— 收敛到一处,不在每个 Connector 里各写一份重试状态机。etcd 里那个后端可能已经彻底没了,Connector 自己是不知道的。

---

## 5. 三个 Server 的关系

| | 角色 | 线程 |
|---|---|---|
| `kcp::Server` | 网关控制面:BPF / Worker 生命周期 / 服务发现 / etcd | 主线程 |
| `kcp::Worker` | 网关数据面:UDP + KCP 会话 + 后端 Connector | 每线程一个 |
| `tcp::Server` | 后端服务端:accept + 派发给 `tcp::Reactor` | 主线程 + N Reactor |

- **数据面**:`客户端 →(KCP/UDP)→ kcp::Worker →(TCP)→ tcp::Server`,反向对称。见 [kcp_server.md](kcp_server.md) / [tcp_server.md](tcp_server.md)。
- **协议**:三段都用 [`core::Package`](package.md) —— 客户端方向只上 `data` 段,网关↔后端上整帧。
