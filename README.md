# typhon

> 基于 KCP 的 C++ MMO 微服务框架 —— 网关 IO 底座 + 后端业务服骨架。

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

typhon 是一个**面向 MMOARPG 的服务端框架**,核心目标是把"客户端 ↔ 网关 ↔ 后端业务服"这条数据通道做到工业级:可靠、低延迟、可水平扩展、可观测。

当前阶段聚焦**网络 IO substrate** —— 业务层(战斗、AOI、持久化等)由上层应用接入。

---

## 关键设计

### 整体拓扑

```
                ┌──────────────┐
   客户端 ──KCP──┤              ├──TCP──┐
                │   网关       │       ├──> backend instance (scene)
   客户端 ──KCP──┤  (gateway)   ├──TCP──┤
                │              │       ├──> backend instance (chat)
   客户端 ──KCP──┤              ├──TCP──┤
                └──────────────┘       └──> backend instance (guild)
                       │
                       └─── etcd (服务发现)
```

- **客户端 ↔ 网关**:KCP over UDP,带 AES-128-CTR payload 加密 + XDP envelope MAC(SipHash)DoS 过滤(均已实现,见下「传输安全层」)
- **网关 ↔ 后端**:TCP 长连,wire frame 走 length-prefix `PackageEx`,网关 stamp `pke_src_id` (FromPlayerID) 让后端拿到来源信息
- **后端 ↔ 后端**:暂未规划(后期通过 etcd 服务发现 + sticky routing 接入)

### 网关层 (`kcp::Server`)

- N 个独立 `kcp::Server` 实例(N ≈ CPU 核数 - 1),每个一条线程
- `SO_REUSEPORT` + **eBPF 程序按 KCP conv 路由**,同一 conv 始终落到同一实例 → **share-nothing,零锁**
- `recvmmsg` / `sendmmsg` 批量 syscall,降低内核切换开销
- 每实例自己的 epoll loop:`ufd` + `stop_evfd`,1 个 `epoll_wait` 拿数据
- session 用 `shared_ptr` 管理,业务可跨调用安全持有

详见 [include/kcp/server.hpp](include/kcp/server.hpp)、[src/kcp/server.cpp](src/kcp/server.cpp)。

### 协议层 (`core::Package` / `core::PackageEx`)

字节序统一**大端**(网络字节序)。两种方向各用一种 wire frame:

```
┌─────────────────────────────────────────────────────┐
│ 客户端 → 网关 (KCP):  Package                       │
├─────────────────────────────────────────────────────┤
│   pk_id (2B) │ pk_idem (4B) │ pk_dst_id (4B)        │
│   pk_payload[...]                                   │
│                                                     │
│   长度由 KCP 消息边界给定 (KCP 自带帧边界,无需长度字段)│
└─────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────┐
│ 网关 → 后端 (TCP):  PackageEx                            │
├─────────────────────────────────────────────────────────┤
│   pke_len (2B) │ pke_src_id (4B) │ pke_src_addr (4B)   │
│   pke_pk[...]  ← 内嵌完整 Package wire frame            │
│                                                         │
│   pke_len = 整个 wire frame 总长,后端 peek 2B 即可切包  │
└─────────────────────────────────────────────────────────┘
```

- `pk_idem`:客户端在 session 内单调递增的幂等 ID,网关侧做 dedup
- `pk_dst_id`:目标服务类型(路由键,scene/chat/guild/...)
- `pke_src_id`:网关从 conv 查到的 FromPlayerID,**后端无需信任客户端身份信息**

详见 [include/core/package.hpp](include/core/package.hpp)。

### 传输安全层

客户端 ↔ 网关这一跳两层独立防护,可分别开关:

**1. envelope MAC —— XDP 内核态 DoS 过滤**

```
UDP payload: [siphash 8B][KCP frame: header 24B + segment data]
```

- UDP payload 前 8B 是 SipHash-2-4 tag,**覆盖 KCP frame 前 24B(KCP header)**
- XDP 在网卡驱动层校验,不过直接 `XDP_DROP` —— 垃圾 / 伪造包**不进 socket、不进 KCP**,不耗 worker CPU
- key 走 BPF map(current / previous 双槽,支持热 rotate 不停服)
- **ikcp.c/.h 一字不改**,envelope 套在 KCP frame 外;sk_reuseport 路由的 conv 读取偏移 +8
- 选 fixed-24B 而非全 frame:避开 eBPF verifier 对 bpf_loop / 大循环展开的限制

**2. AES-128-CTR payload 加密 —— 半加密**

- Package header(10B)**明文**(网关读 `pk_dst_id` 路由 / `pk_idem` 做 IV),只加密 `pk_payload`
- per-packet IV = `[conv 4B][idem 4B][dir 1B][block counter 7B]`,保证 (key, IV) 全局唯一:
  - `conv + idem` 每包唯一;`dir` 区分上 / 下行防跨方向复用;低 7B 留 CTR 递增
- AES-NI 硬件指令实现(无外部库依赖,无 timing side-channel)

两套算法(SipHash / AES-128)都有官方 test vector 验证位等价,客户端([test_kcp.py](examples/kcp_echo/test_kcp.py) 用同 spec 实现)可互操作。

> ⚠️ **已知短板**:CTR 不带认证(payload 可被 bit-flip 定向篡改且不可检测);SipHash 与 AES 当前共享同一 key。生产前需换 **AES-128-GCM**(一步拿完整性)+ **HKDF 派生独立 key**。详见 [PLAN.md](PLAN.md)「传输安全层 → 待办」。

详见 [src/bpf/envelope.bpf.c](src/bpf/envelope.bpf.c)、[src/utils/cryptor.cpp](src/utils/cryptor.cpp)、[src/kcp/session.cpp](src/kcp/session.cpp)。

### 后端层 (`tcp::Server` + `tcp::Proc`)

- 主线程:listen + accept + N 个 Proc worker(线程池)
- 同一 `fd` 始终被 `fd % worker_size` 这条 Proc 处理 → **同一 session 不跨线程,无锁**
- 主线程 ↔ Proc 通信走 **SPSC 无锁队列 + eventfd 唤醒**(`QEvent: Recv / Send / AddSess / RmvSess`)
- Proc 内部:
  - 自己的 epoll loop 听 `que_evfd` + `stop_evfd`
  - 接收 server 主线程 push 的 QEvent,按 fd 路由到本地 Session
  - 自己维护 `tnow_`(woker-local 时间戳,免 cross-core cache contention)
  - 自己维护超时切片扫描 (`i = id_; i += worker_size`),与其他 Proc 物理隔离

详见 [include/tcp/server.hpp](include/tcp/server.hpp)、[include/tcp/proc.hpp](include/tcp/proc.hpp)、[src/tcp/server.cpp](src/tcp/server.cpp)、[src/tcp/proc.cpp](src/tcp/proc.cpp)。

### Session 模型

|  | 网关 KCP Session | 后端 TCP Session |
|---|---|---|
| 容器 | `unordered_map<conv, shared_ptr>` | `shared_ptr[MAX_CONN]` (按 fd 索引) |
| 生命周期 | KcpServer 内部 + 业务可持有 | TcpServer 内部 + 业务可持有 |
| 线程归属 | conv 所属的 kcp::Server 实例线程 | `fd % ws` 所属的 Proc 线程 |
| 业务 hook | `IEvent::on_data / on_connected / on_disconnected` | `PackageHandler` + `IEvent::on_connected / on_disconnected` |
| 业务自定义数据 | TBD(后期补) | `set_user_data` / `get_user_data<T>` |

两边都用 `shared_ptr` 保证业务跨调用持有 Session 不会 UAF。

### 关键工程决策

| 决策 | 选择 | 理由 |
|---|---|---|
| 时间戳类型 | `uint64_t` monotonic ms | 5.84 亿年才 wrap,免去 wrap-safe 算术心智 |
| 时间戳来源 | 每个 worker 自己 `clock_gettime` | 免去 atomic store/load 的 cache contention |
| RcvBuf | lazy `mi_malloc(PKG_MAX_LEN)` + 双游标 + 阈值 compact | 冷连接零开销,热连接零碎片 |
| SPSC 队列 | 自实现,cache-line aligned,producer/consumer 缓存对方游标 | 避免 false sharing,每个 enqueue/dequeue ~5ns |
| KCP wrap 兼容 | KCP 内部仍用 `uint32_t` ms,我们 cast 后传 | `_itimediff` 在 < 24.8 天差距内 wrap-safe,实际场景远不到 |
| Server::tnow 取消 | 每 worker 自己维护 | 消除一个 atomic 共享字段,简化数据结构 |

### 已规避的 race / lifecycle bug

这两天的迭代踩平了一系列隐患,文档化下来给后来人避坑:

- **release 顺序**:`procs_.clear()` 必须在 `threads_.join()` **之后**,否则 worker 线程持有的 `this` 指针变成 UAF
- **epoll_ctl DEL 二次调用**:同一 fd 的 `remove_session` 加幂等检查 `if (sessions_[fd])`,挡掉 EBADF
- **EAGAIN 误判为 EOF**:`recv` 返回 -1 + `EAGAIN/EWOULDBLOCK` 是"读完了"的正常信号,必须单独 `break` 不能 `del = true`
- **fd 跨 worker 扫描 race**:`Proc::check_timeout` 按 `i = id_; i += worker_size` 切片,**worker 之间不会读对方的 sessions_[fd]**
- **EPOLLHUP 持续触发**:server 主线程拿到 HUP 后**立即** `epoll_ctl DEL`,然后再 push RmvSess,避免后续 epoll_wait 重复触发同一事件
- **on_que_handle drain**:eventfd 读到 EAGAIN 后必须 `break` 走到 `sending_.store(false) + drain SPSC`,不能 `return`

详见 [PLAN.md](PLAN.md) 与各文件 doxygen。

---

## 当前进度

- [x] **Phase 1** —— 网关 IO substrate + 端到端 echo(KCP, Unity 客户端跑通)
- [x] **Phase 2a-c** —— 协议层 (Package/PackageEx) + 后端框架 (TcpServer + Proc + SPSC + handler 派发)
- [x] **传输安全层** —— XDP envelope MAC(SipHash-2-4) + AES-128-CTR payload 半加密(均已实现并跑通)
- [x] **Phase 2d 链路骨架** —— TcpConnector(连接状态机 + 非阻塞 connect + EPOLLOUT 续发 + PING 心跳)已实现并挂进 KcpServer epoll(`servs_` / `on_serv_handle`);gateway↔backend 控制面(REGIST_REQ/RSP 握手 + PING/PONG)已跑通
- [ ] **Phase 2d 业务转发** —— `on_data`(KCP→后端:按 `pk_dst_id` 选 Connector + prepend PackageEx) + `on_serv_handle` 业务回程(后端→KCP) ← **下一步**(当前 example 仍是网关本地 echo,`on_serv_handle` 只处理 PONG/REGIST_RSP 控制包)
- [ ] **Phase 2e** —— EchoBackend 进程 + 完整链路端到端跑通(PING/PONG handler 本身已实现)
- [ ] **Phase 2f** —— etcd 服务注册发现:网关 control loop + `NewServ` 事件分发已通(typhon::Server 定时节拍 → `notify` → kcp::Server 建 Connector),**etcd 客户端待填**(`update_serv` 当前硬编码后端地址)
- [ ] **安全层加固** —— AES-128-GCM(payload 完整性) + HKDF 派生独立 key

完整规划见 [PLAN.md](PLAN.md)。

---

## 构建与运行

依赖:
- Linux,kernel ≥ 5.x(XDP envelope 过滤;io_uring 可选需 ≥ 5.10)
- **x86-64 CPU 带 AES-NI**(payload AES-128-CTR 硬件加速,2010 年后的 x86 都有)
- C++20 编译器(g++ ≥ 11 / clang ≥ 13)
- [mimalloc](https://github.com/microsoft/mimalloc) (高性能内存分配器)
- [spdlog](https://github.com/gabime/spdlog) (日志)
- libbpf(eBPF SO_REUSEPORT 路由 + XDP envelope MAC 过滤;clang 编译 BPF 对象)
- (仅压测客户端)OpenSSL `libcrypto`,[test_kcp.py](examples/kcp_echo/test_kcp.py) 用它做 AES-128-CTR

```bash
make                            # 编译 lib + bpf 对象
make -C examples/kcp_echo       # 编译 echo server + 拷 bpf 对象到 build/

cd examples/kcp_echo
# 参数: <ifname> <kcp.bpf.o> <envelope.bpf.o>  (需 root / CAP_BPF 加载 XDP)
sudo ./build/server lo build/kcp.bpf.o build/envelope.bpf.o   # 监听 0.0.0.0:5555

# 跑压测 (另一终端)
python3 test_kcp.py
```

---

## 设计哲学

> **底层稳了,业务才能跑得动**。

- **share-nothing > 加锁**:网关 by conv、后端 by fd,worker 之间物理隔离
- **明确的数据所有权**:Session 由 server 持有,worker 通过 shared_ptr 拿副本,业务跨调用安全
- **fail-fast > silent error**:后端 `ASSERT abort` 暴露 bug,而不是 swallow + 继续运行;客户端入口走 graceful drop
- **KCP 协议不动**:envelope MAC 套在 KCP frame 外、payload 加密在 ikcp_send 之前完成,**ikcp.c/.h 一字不改,上游升级照单全收**
- **时间戳一律 monotonic uint64 ms**:全程一种类型,免 wrap、免 cast
- **接口契约写在 doxygen 里**:并发性、线程归属、生命周期、调用顺序约束都明确文档化

---

## 许可

[MIT](LICENSE) © 2026 xiaoq87722-art
