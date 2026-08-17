# 开发计划 / Roadmap

> 组件命名见 [README](README.md#组件)。已完成项保留为设计记录,随代码演进更新。
>
> 本分支只含基础框架,不排具体产品的业务需求 —— 业务路线在产品分支上各自维护。

## Phase 1 — 网关 IO substrate + 端到端 echo ✅

- [x] 项目骨架(构建系统、目录结构)
- [x] 单线程 UDP echo(`recvmmsg` / `sendmmsg` 批量)
- [x] epoll 事件循环
- [x] 集成 ikcp(单线程 KCP echo)
- [x] 多线程 + `SO_REUSEPORT` + eBPF 按 `conv` 路由(share-nothing 零锁)
- [x] 客户端(Lilith,C#)集成 KCP 收发线程
- [x] 端到端 echo 跑通(多客户端并发各自不串)

---

## Phase 2 — 网关 → 后端转发 ✅

### 2a 线协议:全小端显式 codec ✅
- [x] `Package = meta{len,conv,src_addr} + data{pid,src_id,dst_id,payload}`;线上 `len`/`pid` 2B、其余 4B;`PKG_META_LEN=10 / PKG_DATA_LEN=10 / PKG_HDR_LEN=20`
- [x] 共享 codec:`data_encode/decode`(客户端 data-only 段)、`frame_encode/decode`(网关↔后端整帧,自带分帧)、`token_decode`
- [x] **全小端**(x86/ARM 原生,两端零转换);Buffer 解耦成纯字节缓冲,分帧逻辑在 `frame_decode`
- [x] **零分配收包契约**:`recv(Package*)` 解进调用方 buffer、codec 内部不分配;每线程一块 `alignas(Package) static thread_local` 复用 Package;跨线程转交才 COPY(mimalloc)
- [x] 客户端(Lilith)编解码对齐小端

> 旧的 `PackageEx` 前置封装 + `pk_hton/pke_hton` 字节序系统已删除。

> **应用层 `seq` 也已删除。** 它原本兼两个职责:幂等 dedup 与 AEAD nonce。前者是重复造轮子 —— `ikcp_parse_data` 自带 sn 去重,重复段只回 ACK 不入 `rcv_queue`;后者随安全层下沉,改由信封层的计数器承担。

### 2b 后端框架:tcp::Server + Reactor ✅
- [x] `tcp::Server`(IO 线程):listen + accept + 从 socket 读字节,塞目标 Reactor 的 **MPSC 队列 + eventfd 唤醒**
- [x] `tcp::Reactor`(每线程数据面):自己的 epoll + eventfd + MPSC + 会话表(按 fd)+ 终端表(按 uid);同一 fd 恒由 `fd % worker_size` 处理 → 无跨线程 race
- [x] `tcp::Session`(按 fd):切包状态 + `last_recv_ms` + authed;`recv(Package*)` 解进 buffer
- [x] `regist_handler(pid, handler)` / `regist_handler(mid, handler)` 双注册(裸包 / protobuf 消息),handler 同步消费;Reactor 超时切片扫描(`i = id_; i += ws`)物理隔离
- [ ] PlayerRoutingTable(搁置):若上业务层按 player_id/scene_id 分片,player→worker 由 `id % N` 直接确定,不需全局 map

### 2c 网关:控制面 / 数据面分离 ✅
- [x] `kcp::Server`(控制面,主线程):XDP `EnvelopeFilter` + `sk_reuseport` `Router` + N 个 Worker + 控制面 epoll + etcd 注册
- [x] `kcp::Worker`(每线程数据面):独立 UDP socket(`SO_REUSEPORT`)+ 自己的 epoll + 会话表(按 conv)+ 后端 Connector + **无锁 MPSC 事件队列**
- [x] 会话只按 conv 存(无 user→session 映射);s2c 回程 `conv % N`:本 Worker 直发 / 否则转 owner Worker
- [x] 双向事件通道:主线程→Worker(`Worker::mque_`)与 Worker→主线程(`Server::evque_`)**都是有界 lockfree MPSC + eventfd**,两端各配 park-unpark 双检(只在队列由空变非空时才 `write` eventfd)
- [x] 停机:`Worker::stop` 置 Stopping + `notify(Stop)` 唤醒 epoll,drain 消费 Stop 事件后循环自然退出

### 2d 网关↔后端:Connector + 转发 ✅
- [x] `tcp::Connector`(网关侧 TCP 客户端):非阻塞 connect 状态机 + EPOLLOUT 续发 + PING/PONG 心跳 + 应用层探活;**刻意不做 TCP 层自重连**(重连交服务发现层)
- [x] Connector fd 挂进 Worker epoll(`servs_` / `on_serv_handle`)
- [x] 控制面握手:`REGIST_REQ/RSP`(置 authed)+ `PING/PONG`(打延迟)
- [x] `on_c2s`(上行 KCP→后端):按 `data.dst_id` 选 Connector,`frame_encode` 整帧转发;后端未握手完则 drop;后端找不到/写失败**不踢客户端**
- [x] `on_s2c`(回程 后端→KCP):按 `meta.conv` 找会话;本 Worker 直发,跨 Worker 则 COPY 进 `KcpSendArg` 转 owner
- [x] 单后端端到端跑通:client → on_c2s → 后端 echo → on_s2c → client

> 旧的"零拷贝 prepend PackageEx 头"手法已废,现在是 `frame_encode` 整帧转发 + 跨 worker COPY。

### 2e 服务发现:etcd ✅
- [x] 网关注册 `/{name}/{id:08X}`(如 `/moses/000003E8`),value=`ServerInfo` JSON(含 `nthreads`),TTL 续租
- [x] 后端以 `name = "public/<type>"` 注册(key `/public/<type>/<id>`);网关读 **`/public` 前缀**发现所有后端(`update_serv` → `AddServ` 广播 → 建 Connector)
- [x] 掉线经 pipe 回流 `RmvServ` → 主线程摘除 → 下周期 `update_serv` 重连;**重连风暴防护**:`update_serv` 只在 epoll 超时(n==0)跑,给固定退避
- [x] Eva(登录服)读 `/moses` 前缀返回网关列表给客户端
- [ ] 多 instance + conv 一致性 hash(同 conv 恒落同 instance)
- [ ] instance 加入/退出的连接迁移(新 dial → 老 drain → close)

### 2f 终端生命周期:进入 / 绑定 / 顶号 / 踢除 ✅
- [x] **PID 表(101-114)**:`TER_REG_*` 注册、`TER_KIC_REQ/RSP/NTF` 踢除、`TER_ENT_REQ/RSP` 进入、`TER_LEA_REQ/RSP` 离开、`TER_OFF_NTF` 下线、`TER_BIND_NTF`/`TER_UNBD_NTF` 绑定变更、`TER_ERROR` 请求失败(网关→客户端)
- [x] **`router: true` 服务(Noah)集中仲裁顶号**:`tcp::Directory` + `tcp::Terminal`(按 uid),跨 Reactor 的顶号经 Directory 走消息队列
- [x] **终端进入后端(ENT)**:客户端发 `TER_ENT_REQ` 当扳机(payload 空),网关 `on_terminal_enter_req` **拦截不转发**,用会话状态重新盖章后发给后端;后端 `add_terminal` → RSP 回网关 → 网关 `bind()` 并把 RSP 中继给终端
- [x] 路由服务的 ENT 走 `uid % N`,其余后端按 `binds_` 判定;断线重连自动重报,后端重启由 `terminal_reenter` 覆盖
- [x] **KCP 控制 cmd**:新增 `RST` / `KICK`(带 code),走 cmd 而非应用层消息 —— 绕开 sn 窗口,断连时也能送达;`ikcp.c` 只识别 + 回调,发送逻辑在 `Session`
- [x] 死链判定统一走 `ikcp_update` 返回码(`-1` 超时 / `-2` rst),上游的 `state`/`dead_link` 已删

### 2g 持久化访问层 ✅
- [x] `adam::db::Scylla`(Meyers 单例):`init_from_file` / `connect(keyspace)` / 模板 `exec(cql, binder, reader)`
- [x] cpp-rust-driver 静态库打进 `libadam.a`,下游服务不必单独链
- [ ] `cass_session_prepare` 每条消息都跑一次 → prepared statement 缓存(现在一半的 DB 往返是浪费)
- [ ] 阻塞式 DB 调用仍在 Reactor 线程上 → 独立 DB 线程池 / 异步 future

### 2h 业务消息层 ⏳
- [ ] 业务消息号约定文档
- [ ] scene / chat 等真实业务消息展开(业务分支)

**Phase 2 验证标准**:多客户端 → 网关 → 多后端 instance → 原路返回;同 conv 恒落同 instance;后端上下线网关列表自动更新;PING/PONG 心跳 + idle session 清理。

---

## 传输安全层 ✅

三层独立、可分别开关:**XDP envelope MAC**(内核态 DoS 过滤)+ **X25519 ECDH 握手**(每会话密钥协商)+ **ChaCha20-Poly1305 信封 AEAD**(包住整个 KCP 数据报,机密性+完整性+防重放一体)。

### envelope MAC(XDP DoS 过滤)
- [x] SipHash-2-4 截 64-bit,8B tag prepend 在 KCP frame 前,**只覆盖前 24B(KCP header)**(固定 3-block,绕开 verifier 对动态长度 bounds check 的限制)
- [x] `envelope.bpf.c`(XDP,native→generic fallback)+ `EnvelopeFilter` 加载器;key 走 BPF map 双槽(current/previous,支持热轮换)
- [x] `sk_reuseport`(kcp.bpf.c)读 conv 偏移 **+8**;userland 收包偏移 8B 喂 ikcp,**ikcp.c/.h 一字不改**
- [x] 启动顺序:EnvelopeFilter attach → socket bind → Router attach(XDP 先于 socket 生效)

### X25519 鉴权握手 + 会话密钥
- [x] **AccessToken(Eva 签发,客户端只搬运)**:`ACCESS_TOKEN_LEN=116`,布局 `expire@0 / conv@8 / user_id@12 / ip@16 / cli_pk@20 / sign@52`;Eva 用网关 X25519 公钥 sealedbox 封 + ed25519 签(签明文前 `ACCESS_TOKEN_SIGNED_LEN=52` 字节),绑 conv / 限时
- [x] **REGIST_REQ 校验链**:sealedbox 解密 → 验期 → 验 `conv == s->conv()` → 验 `token.user_id == data.src_id` → **验 `conv % N == user_id % N` 不变量** → ed25519 验签 → 派生密钥 → 回 RSP(REGIST payload = `ACCESS_TOKEN_LEN(116) + 48 = 164`)
- [x] **会话密钥(crypto_kx)**:网关每会话生成临时 X25519,与 `token.cli_pk` 派生双向 `rx/tx`;RSP 回带网关临时公钥,客户端派生出对称密钥;**双边前向保密**(两端 X25519 皆临时)
- [x] 加密激活 = `Session::authed`:握手包(REGIST_REQ/RSP)走明文,authed 翻转后才加解密,两端对称

### 信封 AEAD(ChaCha20-Poly1305,包住整个 KCP 数据报)
- [x] **安全层下沉到 KCP 之外**:UDP 载荷 = `[8B 槽位 MAC][4B conv][4B 计数器][AEAD(24B KCP头 + 数据)][16B tag]`,开销 32B/包,`KCP_MTU` = 1450 − 32 = **1418**
- [x] `conv` 与计数器进 **AAD**(认证但不加密 —— 解密前就要用它们定位会话);`conv` 放偏移 8 与旧 KCP 头里的位置重合,**两个 BPF 程序一个字节都不用改**
- [x] 计数器每次**发送**递增(含重传与纯 ACK),**兼作 AEAD nonce 与防重放序号**;耗尽即拆会话
- [x] 收端 **RFC 6479 式 64 位滑动窗口**去重:容忍乱序(KCP 依赖它),拒绝重放
- [x] per-session 双向密钥(握手 ECDH 派生,各 32B 用满);`dir` 防跨方向复用
- [x] 加密激活 = `Session::authed`,握手期格式为 `[8B MAC][裸 KCP 数据报]`;libsodium 实现,客户端(Lilith / 压测脚本)位等价对齐

> **为什么要下沉。** 改造前 AEAD 在 KCP 之上,KCP 头裸露在外,下面只有分槽共享的 MAC —— 同槽客户端可伪造受害者的 `una`/`rmt_wnd`/`sn`,一个包就能清空对方发送缓冲或劫持下行。根因是**认证发生在错误的层**:攻击面在 `ikcp_input` 里,而认证在它之后。下沉之后这四条注入路径由构造消失。

> `conv` 不变量是关键:Eva 的 `MakeConv`(Redis `INCR` 序号,`(seq%kmax)*N + user%N`)生成的 conv 满足 `conv % N == user_id % N`(N 从 etcd ServerInfo 的 `nthreads` 读),从而经 sk_reuseport 恒落到负责该 user 的 Worker。

### 待办 / 短板
- [ ] envelope MAC key 仍全局静态 → HKDF 派生 + 定时 rotate(`rotate_key()` 接口已就绪,缺定时触发 + 客户端协商)
- [ ] envelope 5-tuple binding(MAC 不含 src_ip/port,可异源重放;AEAD 层的滑动窗口兜底)
- [ ] per-src-IP rate-limit(XDP LRU map 对校验失败源做惩罚 / 黑名单)
- [~] conv 仍由客户端选(token 绑 conv 已挡跨 conv 重放;per-session key 已消除"conv 撞 → nonce 复用")

---

## Phase 3 — 测试与可观测 ⏳

- [x] **火焰图工具接入**:gperftools CPU profiler(config `flame` 开关,输出 `.prof` → `pprof`);profiling build 需 `-fno-omit-frame-pointer`
- [ ] **gtest 单元测试**:codec(`data_/frame_/token_` encode↔decode 往返)、MPSC/SPSC、cryptor 原语、`conv % N == user % N` 不变量
- [ ] **场景测试(kcp / tcp server)**:
  - kcp echo / tcp echo / **网关 → 后端全链路 echo**
  - QPS / 吞吐 / 压力 / 延迟(**tcp 延迟意义不大**——主要测 OS TCP 栈;延迟重点在 kcp 的 ARQ/拥塞)
- [ ] off-CPU 分析(`perf sched` / eBPF `offcputime`)配合延迟测试(on-CPU 火焰图看不到 epoll_wait 阻塞)

---

## 后期优化(profile 数据驱动)

- [ ] **网关 Worker CPU 亲和绑定**(`pthread_setaffinity_np`):BPF conv→Worker N,Worker N 绑核 N,整条数据路径 L1/L2 命中;消除 KCP `update()` 周期调用的调度抖动。后端 Reactor 不做(业务 CPU 不可预测)
- [ ] **`tcp::Reactor` 切 io_uring**(kernel ≥ 5.10):高连接数 + 高频小包场景大幅降 syscall 开销;时机 = profile 显示 epoll/recv/send syscall 占 Worker CPU > 20%;先切 Reactor(per-worker 一个 ring),accept 主线程保留 epoll
- [ ] **部署拓扑**:网关独立 EC2(c5n/c6gn 网络优化型 + CPU pinning)、后端 Docker 独立 EC2(同 VPC)、etcd 单独一组;同 VPC RTT < 1ms,容器化收益 > 损耗
