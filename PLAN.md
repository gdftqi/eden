# typhon 开发计划

> 基于 KCP 的 C++ MMO 微服务框架。

## Phase 1 — 网关 IO substrate + 端到端 echo

**目标**：网关下行 IO 全套打通，C# Unity 客户端通过 KCP 与网关跑通 echo。

**任务**：
- [x] 项目骨架（构建系统、目录结构、hello-world 编译跑通）
- [x] 单线程 UDP echo（recvmmsg / sendmmsg）
- [x] epoll 事件循环
- [x] 集成 ikcp（单线程 KCP echo）
- [x] 多线程 + SO_REUSEPORT + eBPF 按 conv 路由（share-nothing 零锁）
- [x] recvmmsg / sendmmsg 批量化
- [x] C# Unity 客户端集成 KCP（kcp2k 低层 + 自写 KcpSession 收发线程）
- [x] 端到端 echo 跑通（Python 100 并发 × 4KB + Unity 单客户端 4KB 周期 echo）

**验证标准**：C# 客户端发字符串到网关，网关原样返回；多客户端并发各自不串。

---

## Phase 2 — 网关 → 后端转发

**目标**：客户端 KCP 消息经网关 stamp 后转发到后端业务服，后端处理后原路返回客户端。

### 2a. 消息协议层

- [x] Package（12B 头）+ PackageTail（8B 网关 stamp）
- [x] 字节序转换（pk_hton / pk_ntoh / pkt_hton / pkt_ntoh）
- [x] KCP session 幂等校验（rcv_idem_ 单调递增）
- [x] Kcp::ctor 初始化 last_recv_ms_，避免新 session 立即超时被踢
- [x] C# Unity 客户端消息格式与服务端对齐（Package 编解码 + idem 单调递增）

### 2b. 后端服务框架（基础骨架）

- [x] TcpServer：listen + accept + 按 `fd % N` 派发到 worker
- [x] TcpWorker：1 个 IO 线程 + SPSC 队列接收 RcvBuf + 双 eventfd（stop / que）

### 2c. TcpSession + 消息派发

- [x] TcpSession 抽象：per-fd 切包状态（PkgBuf with rpos/wpos）+ last_recv_ms + rcv_idem + authed 标志
- [x] PkgBuf：65535 字节线性 buffer + 阈值压缩；decode 兼容 client→gateway / gateway→backend 双向 pk_len 语义
- [x] TcpSession::recv 三态返回：`1` 真包 / `0` 重复或非法（已 drain）/ `-1` buffer 无完整包
- [x] **统一队列事件 `QEvent { Recv, AddSess, RmvSess }`** —— 取代单一 RcvBuf 队列
- [x] TcpServer 维护 `TcpSession* sessions_[65536]`（raw 指针数组，按 fd 索引）
- [x] **session lifecycle 全部走 worker 队列**：server 主线程只 push `AddSess / Recv / RmvSess` 事件，worker 串行处理
      → 同一 fd 的事件由 SPSC FIFO 保证有序，`sessions_[fd]` 元素只被一个 worker 读写，**消除跨线程 race**
- [x] `handlers[65536]`：按 pk_id O(1) 派发，存于 TcpServer，所有 worker 共享读（启动期注册后不变）
- [x] 注册接口 `regist_handler(pk_id, fn)`
- [x] worker 退出前 drain 队列释放残留 RcvBuf；TcpServer release 时统一 delete 残留 TcpSession
- [ ] `PlayerRoutingTable`（暂搁置）：原计划用 `tbb::concurrent_hash_map<player_id, ClientInfo>`，
      若 Phase 3 上 Lua 业务层 + 按 player_id 或 scene_id 分片，则不再需要此全局 map
      （player → worker 由公式 `id % N` 直接确定，跨 worker 通信走 mailbox 消息）

### 2d. TcpConnector + KcpServer ↔ TcpServer 集成

- [ ] **TcpConnector**：网关侧 TCP 长连客户端
      - 非阻塞 `connect()`，状态机:`Disconnected` / `Connecting` / `Connected`
      - inbuf 复用 PkgBuf，按"网关→后端"方向(pk_len 含 tail)切包
      - outbuf + EPOLLOUT 处理 partial write
      - 重连(指数退避)留接口,2d 阶段可先不实现完整重连
- [ ] TcpConnector fd 加入 KcpServer 主 epoll，主循环按 fd 分发到 `on_backend_handle`
- [ ] `on_data`(KCP → backend)：按 `pk_dst_id` 选 TcpConnector → stamp PackageTail（pkt_src_id / pkt_src_addr） + 重写 pk_len 含 tail → write
- [ ] `on_backend_handle`(backend → KCP)：TcpConnector inbuf decode → 查 KcpServer.sessions_(conv 表) → `kcp->send_pk` 回客户端
- [ ] 单 backend instance 端到端跑通 (KcpServer 上挂 1 个 TcpConnector,目标地址硬编码)

### 2e. Echo + PING/PONG

- [ ] EchoBackend 进程（用 TcpServer + TcpWorker + 一个 echo handler）
- [ ] 跑通完整链路：client → KcpServer → TcpConnector → TcpServer → handler → 反向
- [ ] PING / PONG handler + Session.last_recv_ms 心跳判定
- [ ] 验证 handlers[] 派发机制 + 心跳超时清理 session

### 2f. ETCD 服务注册与发现 + REGIST 握手

- [ ] ServiceRegistry 模块封装 etcd 客户端
- [ ] 后端启动注册 service_type + addr + lease，定期续约
- [ ] 网关 watch service_type 前缀，动态维护 backends_
- [ ] 多 instance + conv 一致性 hash（同 conv 始终落同 instance）
- [ ] REGIST_REQ / REGIST_RSP 握手协议（backend 上线后跟 gateway 握手）
- [ ] instance 加入 / 退出时的连接迁移（新 instance dial → 老 instance drain → close）

### 2g. 业务消息层（占位）

- [ ] 业务消息号约定文档
- [ ] 真正的 scene / chat 等服务消息按业务展开

---

**Phase 2 验证标准**：

- 多客户端 → 网关 → 多个后端 instance → 原路返回
- 同 conv 始终落同一 instance
- 后端进程上下线，网关 backends 列表自动更新
- PING/PONG 心跳工作，长 idle session 被正确清理

---

## 后期优化（待 profile 数据驱动）

- [x] eBPF SO_REUSEPORT 路由（已随 Phase 1 完成）
- [ ] **网关 worker CPU 亲和性绑定（pthread_setaffinity_np）**
  - 仅作用于 KcpServer worker；后端 TcpWorker 不做（业务 CPU 不可预测，pinning 收益低）
  - 与 BPF 路由配合：BPF 把 conv→worker N，worker N 绑核 N，整条数据路径 L1/L2 cache 命中
  - 主要消除 KCP `update()` 周期调用的调度抖动
- [ ] **部署拓扑**
  - MVP / 开发：网关 + 后端 Docker 同 EC2，localhost 通信，便于调试
  - 生产：网关独立 EC2（c5n/c6gn 网络优化型 + CPU pinning），后端 Docker 跑独立 EC2（同 VPC），etcd 单独一组
  - 同 VPC 内 RTT < 1ms，Docker overhead < 1%，容器化收益（滚动升级 / 隔离 / 镜像分发）远大于损耗
- [ ] **TcpServer 切换到 io_uring**（需要 kernel ≥ 5.10）
  - 当前实现：epoll + 非阻塞 recv/send/accept，每次 syscall 都进出内核
  - io_uring：submission queue 一次性批量提交多个 op，completion queue 异步收割，单 syscall 处理 N 个连接的 I/O → **大幅降低 syscall 开销**
  - 优势在**高连接数 + 高频小包**场景(典型 MMO 后端);本机基准能看到 30-50% syscall CPU 下降
  - 代价：编程模型变(从"被动 epoll_wait → 主动处理"变成"submit → poll cqe"),代码改动较大
  - 时机：profile 数据显示 epoll/recv/send syscall 占 worker CPU > 20% 时再上
  - 实施提示：先在 TcpWorker 切换(per-worker 一个 ring),TcpServer 主线程(accept)可保留 epoll 不动
