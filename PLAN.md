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

- [ ] TcpSession 抽象：per-fd 切包状态（inbuf + cursor）+ last_recv_ms / last_send_ms + 可选 outbuf
- [ ] TcpWorker 内部维护 `unordered_map<fd, TcpSession::Ptr>`
- [ ] `handlers[65536]`：按 pk_id O(1) 派发到注册的 handler（uint16 完整空间，~512KB per worker）
- [ ] 注册接口 `register_handler(pk_id, fn)`

### 2d. KcpServer ↔ TcpServer 集成

- [ ] BackendConn：网关侧 TCP 长连客户端，非阻塞 connect / read / write，inbuf 切包 + outbuf + EPOLLOUT
- [ ] BackendConn fd 加入 KcpServer 主 epoll，主循环按 fd 分发到 `on_backend_handle`
- [ ] on_data：按 pk_dst_id 选 backend → stamp PackageTail（pkt_src_id / pkt_src_addr）→ write
- [ ] on_backend_handle：从 BackendConn inbuf 切完整 Package → 查 sessions_ → kcp->send_pk 回客户端
- [ ] 单 backend instance 端到端跑通

### 2e. Echo + PING/PONG

- [ ] EchoBackend 进程（用 TcpServer + TcpWorker + 一个 echo handler）
- [ ] 跑通完整链路：client → KcpServer → BackendConn → TcpServer → handler → 反向
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
