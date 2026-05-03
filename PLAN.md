# typhon 开发计划

> 基于 KCP 的 C++ MMO 微服务框架。

## Phase 1 — 网关 IO substrate + 端到端 echo

**目标**：网关下行 IO 全套打通，C# Unity 客户端通过 KCP 与网关跑通 echo。

**任务**：
- [ ] 项目骨架（构建系统、目录结构、hello-world 编译跑通）
- [ ] 单线程 UDP echo（bind + recvfrom / sendto）
- [ ] 加 epoll 事件循环
- [ ] 集成 ikcp（单线程 KCP echo）
- [ ] 多线程 + SO_REUSEPORT（每线程一个 UDP socket，conv 哈希到固定线程）
- [ ] 加 recvmmsg / sendmmsg 批量化
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
