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

- [x] **Package（10B 头,客户端↔网关 KCP 方向）+ PackageEx（10B 头,网关→后端 TCP 方向）**
      - 旧设计是 Package(12B) + 挂尾 PackageTail(8B) + 回写 pk_len;
        已重构为 **PackageEx 前置封装内嵌 Package**(`pke_pk[]` FAM),网关 prepend 10B 头,
        wire 是标准 length-prefix(`pke_len` 在最前 2B),后端 peek 2B 即可切包
      - Package 自身**不带长度字段**:KCP 方向由消息边界给定,TCP 方向由 pke_len 推
- [x] 字节序转换（pk_hton / pk_ntoh / pke_hton / pke_ntoh,只翻头部字段不递归翻内嵌）
- [x] KCP session 幂等校验（rcv_idem_ 单调递增）
- [x] Kcp::ctor 初始化 last_recv_ms_，避免新 session 立即超时被踢
- [x] C# Unity 客户端消息格式与服务端对齐（Package 编解码 + idem 单调递增）
- [x] SipHash-2-4 实现（utils/cryptor，64 个官方 vector 验证位等价）
- [x] AES-128-CTR 实现（utils/cryptor，AES-NI intrinsics，NIST SP800-38A vector 验证）

### 2b. 后端服务框架（基础骨架）

- [x] TcpServer：listen + accept + 按 `fd % N` 派发到 worker
- [x] tcp::Proc：1 个 IO 线程 + SPSC 队列接收 RcvBuf + 双 eventfd（stop / que）

### 2c. TcpSession + 消息派发

- [x] tcp::Session 抽象：per-fd 切包状态（RcvBuf with rpos/wpos）+ last_recv_ms + authed + user_data
- [x] RcvBuf：lazy 分配 PKG_MAX_LEN 线性 buffer + 阈值 compact；decode 解 PackageEx(peek `pke_len` 2B 切包)
- [x] tcp::Session::recv 三态返回：`1` 真包 / `0`(已废弃,半加密后无 idem 重复态) / `<0` buffer 无完整包或协议错
- [x] **统一队列事件 `QEvent { Recv, Send, AddSess, RmvSess }`**(SPSC 无锁队列 + eventfd 唤醒)
- [x] TcpServer 维护 `Session::Ptr sessions_[MAX_CONN]`（shared_ptr 数组，按 fd 索引;业务可跨调用安全持有）
- [x] **session lifecycle 全部走 worker(Proc)队列**：server 主线程只 push `AddSess / Recv / Send / RmvSess`，Proc 串行处理
      → 同一 fd 始终落同一 Proc(`fd % ws`)，`sessions_[fd]` 只被该 Proc 读写，**消除跨线程 race**
- [x] `handlers[MAX_HANDLERS]`：按 pk_id O(1) 派发，存于 TcpServer，所有 Proc 共享读（启动期注册后不变）
- [x] 注册接口 `regist_handler(pk_id, fn)`
- [x] Proc 退出前 drain 队列释放残留 RcvArg；TcpServer release 时遍历 sessions_ 调 on_disconnected 再清空
- [ ] `PlayerRoutingTable`（暂搁置）：原计划用 `tbb::concurrent_hash_map<player_id, ClientInfo>`，
      若 Phase 3 上 Lua 业务层 + 按 player_id 或 scene_id 分片，则不再需要此全局 map
      （player → worker 由公式 `id % N` 直接确定，跨 worker 通信走 mailbox 消息）

### 2d. TcpConnector + KcpServer ↔ TcpServer 集成

- [x] **TcpConnector**：网关侧 TCP 长连客户端([tcp/connector.hpp](include/tcp/connector.hpp))
      - 非阻塞 `connect()`，状态机:`Disconnected` / `Connecting` / `Connected`(EPOLLOUT 驱动 Connecting→Connected)
      - inbuf 复用 RcvBuf(`recv` 内部 decode PackageEx + ntoh → host 序)
      - outbuf(`sbuf_`) + EPOLLOUT 处理 partial write(`send(now)` 续 flush)
      - [ ] 重连(指数退避)**仍未实现**,`on_serv_handle` 拿到 ERR/HUP 直接 `remove_serv`(注释里留了退避位)
- [x] TcpConnector fd 加入 KcpServer 主 epoll(`add_serv`/`servs_`)，主循环非 ufd/evfd 的 fd 分发到 `on_serv_handle`
- [x] **gateway↔backend 控制面**(原 PLAN 未单列)：Connector `regist()` 发 REGIST_REQ + `update()` 发 PING 心跳;
      `on_serv_handle` EPOLLIN 收包后 dispatch `on_regist_rsp`(置 authed)/`on_pong`(打延迟)
- [ ] `on_data`(KCP → backend)：按 `pk_dst_id` 选 TcpConnector → **prepend PackageEx 头**(`pke_len` / `pke_src_id` = FromPlayerID / `pke_src_addr`),内嵌原始 Package wire frame → write
      —— **未实现**,当前 `on_data` 是 IEvent 虚函数,example 里只做网关本地 echo,没有按 `pk_dst_id` 选 Connector 转发的框架代码
- [ ] `on_serv_handle` 业务回程(backend → KCP)：decode PackageEx → 剥头取内嵌 Package → 查 KcpServer.sessions_(conv 表) → `kcp->send_pk` 回客户端
      —— **未实现**,当前 `on_serv_handle` 只 dispatch PONG / REGIST_RSP 两类控制包,业务包没有回程路由
- [ ] 单 backend instance 端到端跑通 (KcpServer 上挂 1 个 TcpConnector,目标地址硬编码)
      —— 连接 + 握手 + 心跳已跑通,**业务数据面端到端未通**(缺上面两条转发)

### 2e. Echo + PING/PONG

- [x] PING / PONG handler + REGIST_REQ / REGIST_RSP 握手已实现
      - 后端 `Proc::on_ping`(回 PONG) / `Proc::on_regist`(set_id + 回 REGIST_RSP)
      - 网关 `kcp::Server::on_pong`(打延迟) / `on_regist_rsp`(置 `Connector::authed`)
      - 心跳判定:Connector `update()` 两级超时(`last_send`/`last_recv`),Proc `check_timeout` 按 `last_recv_ms`
- [ ] EchoBackend 进程（用 TcpServer + tcp::Proc + 一个 echo handler）—— 尚无独立后端 example
- [ ] 跑通完整链路：client → KcpServer → TcpConnector → TcpServer → handler → 反向(依赖 2d 业务转发)
- [ ] 验证 handlers[] 派发机制 + 心跳超时清理 session

### 2f. ETCD 服务注册与发现 + REGIST 握手

- [x] **网关侧 control loop 骨架**：typhon::Server 主线程兼 control 线程
      - epoll_wait timeout(实测 `INTERVAL_MS = 10000`,10s) 当定时节拍 + stop_evfd 即时唤醒退出
      - kcp::Server 加 `evque_`(SPSC) + `notify()` —— typhon 主线程是唯一生产者,
        单生产者约束成立;QEvent(Stop/**NewServ**/Recv/Send/AddSess/RmvSess) 抽到 core/qevent.hpp
      - 分发已落地:`update_serv()` 每个节拍 `notify(NewServ, NewServArg{id,host})`,
        kcp::Server `on_new_serv` → `Connector::create` → `add_serv`(按 id 去重)
      - ⚠️ 与原设计的偏差:**没有用 `shared_ptr<const BackendTable>` 快照**,改为
        **逐 serv 的 `NewServArg{id, host[32]}` 事件 + `ServMap servs_`**(每个后端一个 Connector)
- [x] REGIST_REQ / REGIST_RSP 握手协议（backend 上线后跟 gateway 握手）—— 见 2e,已实现
- [ ] **etcd 客户端**：用 **HTTP/JSON gateway**(cpp-httplib + nlohmann/json,**不用 gRPC**)
      - 定时 `POST /v3/kv/range` 拉 service_type 前缀全量(key/value base64,调用设超时)
      - 不用 watch(定时 poll 足够,简单无重连/revision 复杂度)
      - **当前是 stub**：`update_serv()` 里 `// TODO: 改为ETCD 查询服务`,硬编码 id=10000 / host="127.0.0.1:6688"
- [ ] 后端启动注册 service_type + addr + lease，定期续约
- [ ] 多 instance + conv 一致性 hash（同 conv 始终落同 instance）
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

## 传输安全层（已实现）

两层独立、可分别开关:**XDP envelope MAC**(内核态 DoS 过滤) + **AES-128-CTR**(payload 加密)。

### envelope MAC（XDP DoS 过滤）

- [x] **SipHash-2-4 截 64-bit**,8B tag prepend 在 KCP frame 前
      ```
      UDP payload: [siphash 8B][KCP frame: header 24B + segment data]
      ```
- [x] **MAC 只覆盖 KCP frame 前 24B（= KCP header）**,不是整个 frame
      - 攻击者必须猜对 conv/sn/cmd 才能伪造合法 MAC,24B 足够
      - payload 完整性交应用层加密;24B = 3 个 SipHash block,XDP 全展开 verifier 秒过、无 tail
      - 选 fixed-24B 而非全 frame:bpf_loop / 全展开 153 block 撞 verifier
        (callback 被内联、dynamic 长度的 packet bounds check 过不了),固定 3-block 绕开
- [x] **envelope.bpf.c（XDP）+ EnvelopeFilter（userland 加载器,bpf/envelope_filter）**
      - attach 先试 `XDP_DRV_MODE`(native),失败 fallback `XDP_SKB_MODE`(generic,lo 上走这个)
      - key 走 BPF map(`BPF_MAP_TYPE_ARRAY` 2 槽:current/previous),`rotate_key()` 旧 key 搬 slot1
      - `target_port` 走 `.rodata`(const volatile),userland 从 host 字符串解析端口写入
- [x] sk_reuseport（kcp.bpf.c）conv 读取偏移 **+8**（MAC 在 conv 之前）
- [x] userland send（output 回调）prepend MAC / recv（on_udp_handle）偏移 8B 喂 ikcp_input,
      **ikcp.c/.h 一字不改**
- [x] 启动顺序:EnvelopeFilter attach → KcpServer socket bind → Router attach
      （XDP 先于 socket 生效,启动期被攻击也挡得住）

### payload 加密（AES-128-CTR 半加密）

- [x] **半加密**:Package header（10B）明文,只加密 pk_payload
      - header 明文让网关读 pk_dst_id 路由 / pk_idem 做 IV;header 完整性已由 envelope MAC 覆盖
- [x] **per-packet IV**:`[conv 4B][idem 4B][dir 1B][block counter 7B=0]`
      - conv+idem 保证 (key,IV) 唯一;dir(0 上行/1 下行)防同 conv+idem 跨方向 keystream 复用;
        低 7B 留给 CTR 内部 block counter 递增,与高位不重叠
- [x] send_pk 加密(DIR_S2C)/ recv_pk 解密(DIR_C2S);非法 / 重放包先 drop 不浪费解密
- [x] AES-NI 实现(utils/cryptor) + test_kcp.py 客户端对齐
      (ctypes 调 libcrypto `EVP_aes_128_ctr`,IV 布局 / 方向 / key 与服务端一致)

### 待办 / 已知短板

- [ ] **payload 完整性**:AES-CTR 不带认证,bit-flip 可定向改游戏数据且不可检测
      → 换 **AES-128-GCM**(一步拿机密性+完整性)或 encrypt-then-MAC ★最高优先
- [ ] **key 复用**:SipHash envelope key 与 AES key 当前共享 `shkey()`
      → HKDF 从 master key 派生独立 mac_key / enc_key
- [ ] **conv 服务端分配**:conv 现由客户端选,两客户端 conv 撞 + idem 撞 → IV 复用
      → 服务端分配唯一 conv,或 per-session 派生 key
- [ ] **envelope 5-tuple binding**:MAC 当前不含 src_ip/port,可被异源重放(idem 去重兜底)
- [ ] **per-src-IP rate-limit**:XDP LRU map 对 MAC 校验失败源做指数惩罚 / blacklist
- [ ] **secret 自动 rotate**:`rotate_key()` 接口已就绪,缺定时触发 + 客户端 key 协商

---

## 后期优化（待 profile 数据驱动）

- [x] eBPF SO_REUSEPORT 路由（已随 Phase 1 完成）
- [ ] **网关 worker CPU 亲和性绑定（pthread_setaffinity_np）**
  - 仅作用于 KcpServer worker；后端 tcp::Proc 不做（业务 CPU 不可预测，pinning 收益低）
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
  - 实施提示：先在 tcp::Proc 切换(per-worker 一个 ring),TcpServer 主线程(accept)可保留 epoll 不动

- [x] **XDP envelope MAC：内核层 DoS 过滤** —— 已实现,详见上方「传输安全层」章节
