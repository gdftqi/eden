# typhon 开发计划

> 基于 KCP 的 C++ MMO 微服务框架。

## Phase 1 — 网关 IO substrate + 端到端 echo

**目标**：网关下行 IO 全套打通，C# Unity 客户端通过 KCP 与网关跑通 echo。

**任务**：
- [x] 项目骨架（构建系统、目录结构、hello-world 编译跑通）
- [x] 单线程 UDP echo（实际用了 recvmmsg / sendmmsg 替代 recvfrom / sendto）
- [x] 加 epoll 事件循环
- [ ] 集成 ikcp（单线程 KCP echo）
- [ ] 多线程 + SO_REUSEPORT（每线程一个 UDP socket，conv 哈希到固定线程；MVP 阶段用 concurrent map + per-session mutex 兜底 endpoint 漂移）
- [x] 加 recvmmsg / sendmmsg 批量化
- [ ] C# Unity 客户端集成 KCP
- [ ] 端到端 echo 跑通

**验证标准**：C# 客户端发字符串到网关，网关原样返回；多客户端并发各自不串。

---

## Phase 2 — 网关 → 后端转发 + 消息结构

**目标**：定义自定义消息帧，写一个后端 echo 服务（TCP 连网关），消息经网关路由到后端再回到客户端。

**任务**：
- [ ] 定义自定义二进制消息帧（含 ToServiceID、FromPlayerID 等头部字段）
- [ ] 网关 ↔ 后端 TCP 长连接（心跳、重连）
- [ ] 路由逻辑：网关按消息头 + 路由键 hash 到后端实例
- [ ] 后端 echo 服务
- [ ] 端到端转发跑通

**验证标准**：客户端发的消息经网关转发到后端 echo 服，后端原样回到客户端。

---

## 后期优化（待 profile 数据驱动，不阻塞 Phase 1/2）

仅在性能瓶颈实际出现时再实施，不要为想象中的并发提前优化。

- [ ] **eBPF SO_REUSEPORT 路由（按 KCP conv 在内核分流）**
  - 目的：替代 Phase 1 Step 4 的 concurrent map + per-session mutex，保证同一 conv 永远落同一 worker，回到完美 share-nothing
  - 触发条件：mutex 竞争 / cache miss 成为瓶颈，或单进程目标 CCU > 10k 时
  - 已就绪：[src/kcp.bpf.c](src/kcp.bpf.c) 草稿；libbpf-dev 系统已装；当前**不参与编译**（移出 src/ glob 或单独 clang 规则）
  - 待做：clang -target bpf 编译规则、libbpf 加载/挂载代码、CAP_BPF 部署、bpftool 调试链路
  - 内核要求：≥ 4.10（建议 5.x）

