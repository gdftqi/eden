# `typhon::Server` — 顶层编排 + 控制面

> 整个网关进程的入口。负责:**装载 BPF(XDP + sk_reuseport)** → 起 **N 个 [kcp::Server](kcp_server.md) worker** → 跑**控制面 loop**(服务发现 + worker 掉线回流)。
> 它自己是单线程(主线程兼 control 线程),不碰数据面 —— 数据面在 N 个 worker 里。
> 源码:[include/typhon.hpp](../include/typhon.hpp) · [src/typhon.cpp](../src/typhon.cpp)

---

## 1. 进程结构总览

```plantuml
@startuml
hide empty members

class "typhon::Server (主/control 线程)" as T {
  - SOCKET epfd_, evrfd_, evwfd_  ' pipe: worker→主 回流
  - EnvelopeFilter envelope_       ' XDP MAC 过滤(加载器)
  - Router router_                 ' sk_reuseport 路由(加载器)
  - vector<kcp::Server::Ptr> ks_pool_
  - vector<thread> threads_
  - ServSet servs_                 ' 已广播的后端 serv_id (去重)
  --
  run(): 启动序列 + control loop
  update_serv(): 周期服务发现
  notify_serv_disconnected(id): 写 pipe(worker 调)
}

class "kcp::Server #0..N" as K {
  独占线程 + SO_REUSEPORT socket
}

T "1" *-- "N" K : ks_pool_ (起线程跑 run)
T ..> "XDP / sk_reuseport\n(内核 BPF)" : envelope_ / router_

note bottom of K
  每个 worker 持 onwer_ = &typhon::Server
  掉线时 notify_serv_disconnected 经 pipe 回流
end note
@enduml
```

---

## 2. 启动序列(**顺序敏感**)

XDP 必须先于 socket bind 生效 —— "开机即受保护",启动期被攻击也挡得住:

```plantuml
@startuml
start
:1. EnvelopeFilter.init + attach(网卡)\n   XDP envelope MAC 校验先挂载;
note right: 必须在 socket bind 之前\n否则启动期垃圾流量直进内核
:2. Router.init(kcp.bpf.o)\n   写 num_workers 到 .rodata;
:3. 创建 N 个 kcp::Server\n   各自 udp_bind(SO_REUSEPORT), owner = this;
:4. Router.register_socket(i, fd) ×N\n   + Router.attach()  把 sk_reuseport 挂上;
:5. 起 N 个 worker 线程 (kcp::Server::run);
:6. init(): epoll + pipe(evrfd/evwfd);
:7. control loop;
stop
@enduml
```

> 析构顺序刚好相反:停 worker → join → 卸 BPF。`stop()` 把 `state_` 翻 `Stopping` 并 `notify(0)` 敲 pipe 唤醒主线程退出。

---

## 3. 控制面 loop + 双向事件通道

主线程 `epoll_wait(timeout = 10s)`:**超时(n==0)** 跑周期服务发现,**有事件(n>0)** 处理 worker 回流。两个方向用**不同通道**,各自满足单生产者:

```plantuml
@startuml
participant "kcp::Server worker" as W
participant "主线程 control loop" as M
participant "ETCD(TODO)" as E

== 主 → worker: 下发后端 (AddServ) ==
M -> E : (周期) 查后端列表\n当前: 硬编码 10000/127.0.0.1:6688
M -> M : update_serv(): servs_ 去重
M -> W : kcp::Server::notify(AddServ)\n[SPSC evque_, 主线程单生产者]
W -> W : on_new_serv → Connector::create → add_serv

== worker → 主: 掉线回流 (RmvServ) ==
W -> W : Connector 掉线(HUP/心跳判死)
W -> M : notify_serv_disconnected(serv_id)\n[**pipe**, 多 worker write≤PIPE_BUF 原子]
M -> M : on_event_handle: 读 pipe\nservs_.erase(serv_id)
note over M: 下个周期 update_serv 发现\nservs_ 没它 → 重新广播 AddServ 重连
@enduml
```

| 方向 | 通道 | 为什么 |
|---|---|---|
| 主 → worker | SPSC `evque_` (kcp::Server 内) | 生产者只有主线程 `update_serv`,单生产者约束成立 |
| worker → 主 | **pipe** (`evrfd_`/`evwfd_`) | N 个 worker 都要写,`write ≤ PIPE_BUF` 天然原子,**不能用 SPSC**(多生产者违约);低频控制平面,不抠纳秒 |

---

## 4. 服务发现 + 重连(`update_serv`)

```plantuml
@startuml
start
:epoll_wait(10s);
if (n == 0 (超时)?) then (是)
  :update_serv();
  note right
    周期服务发现(ETCD TODO)
    · servs_.count(id)? 已广播 → 跳过(去重)
    · 否则 notify(AddServ) 给所有 worker + servs_.insert
  end note
else (n > 0, 有事件)
  :on_event_handle();
  note right
    读 pipe (每 4B 一个 serv_id)
    · id==0 → stop
    · 否则 servs_.erase(id) (掉线, 下轮重连)
  end note
endif
stop
@enduml
```

**重连风暴防护**:`update_serv` **只在 epoll 超时(n==0)分支跑**,不被掉线 pipe 事件即时驱动 —— 给重连一个 `INTERVAL_MS(10s)` 固定退避。否则 "断开 → notify → 立即重连 → 又断" 会把主线程钉成 busy-loop。

> **不做 Connector 层 TCP 自重连**(刻意):掉线即摘,重连统一由服务发现层 `update_serv` 周期重广播驱动 —— 把重连收敛到一处,不在每个 Connector 里写重试状态机。

---

## 5. 三个 Server 的关系一图收尾

```plantuml
@startuml
hide empty members
rectangle "客户端" as C
rectangle "typhon::Server\n(主/control 线程)" as T
rectangle "kcp::Server ×N\n(网关 worker)" as K
rectangle "tcp::Server\n(后端 instance)" as B

C -down-> K : KCP/UDP\n(envelope MAC + ChaCha20)
K -down-> B : TCP\n(PackageEx, 明文)
T -right-> K : 起线程 + AddServ(SPSC)
K -left-> T : 掉线回流(pipe)
T ..> "etcd(TODO)" : 服务发现
@enduml
```

- **数据面**:`客户端 →(KCP)→ kcp::Server →(TCP)→ tcp::Server`,反向对称。详见 [kcp_server.md](kcp_server.md) / [tcp_server.md](tcp_server.md)。
- **控制面**:`typhon::Server` 编排 worker 生命周期 + 后端服务发现。
- **协议**:三段都用 [Package / PackageEx](package.md)。
