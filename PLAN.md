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

**目标**：客户端 KCP 消息经网关 stamp 后转发到后端，再原路返回客户端。

**任务**：
- [x] 消息帧：Package（12B 头）+ PackageTail（8B 网关 stamp）
- [x] 字节序转换（pk_hton / pk_ntoh / pkt_hton / pkt_ntoh）
- [x] 幂等校验（rcv_idem_ 单调递增）
- [x] Kcp::ctor 初始化 last_recv_ms_，避免新 session 立即超时被踢
- [x] C# Unity 客户端消息格式与服务端对齐（Package 编解码 + idem 单调递增）
- [ ] BackendConn：非阻塞 TCP 长连，inbuf 切包 + outbuf + EPOLLOUT
- [ ] BackendConn fd 加入 KcpServer 主 epoll，按 fd 分发
- [ ] on_data：按 pk_dst_id 选 backend → stamp PackageTail → write
- [ ] on_backend_handle：切包 → 查 sessions_ → kcp->send_pk 回客户端
- [ ] 后端 echo 服务
- [ ] 多 instance + conv 一致性 hash
- [ ] 重连（指数退避）+ 心跳
- [ ] etcd 服务注册与发现

**验证标准**：
- 多客户端 → 网关 → 多个后端 instance → 原路返回
- 同 conv 始终落同一 instance
- 后端进程上下线，网关 backends 列表自动更新

---

## 后期优化（待 profile 数据驱动）

- [x] eBPF SO_REUSEPORT 路由（已随 Phase 1 完成）
