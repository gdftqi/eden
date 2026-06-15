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
- [x] ChaCha20-Poly1305 AEAD 实现（utils/cryptor，libsodium `crypto_aead_chacha20poly1305_ietf`;client OpenSSL `EVP_chacha20_poly1305` 位等价验证）

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
      - [x] Connector **刻意不做 TCP 层自重连**(避免每连接一套重试状态机);掉线 `remove_serv` 经 pipe 回流通知主线程,重连由服务发现层(`update_serv` 周期重广播)驱动(见 2f)
- [x] TcpConnector fd 加入 KcpServer 主 epoll(`add_serv`/`servs_`)，主循环非 ufd/evfd 的 fd 分发到 `on_serv_handle`
- [x] **gateway↔backend 控制面**(原 PLAN 未单列)：Connector `regist()` 发 REGIST_REQ + `update()` 发 PING 心跳;
      `on_serv_handle` EPOLLIN 收包后 dispatch `on_regist_rsp`(置 authed)/`on_pong`(打延迟)
- [x] `on_c2s`(KCP → backend,原 PLAN 叫 on_data)：按 `pk_dst_id` 选 Connector(`get_serv`) → **零拷贝 prepend PackageEx 头**转发
      - 零拷贝手法:`on_udp_handle` 解码时 body 落在 `rbuf + PKX_HDR_LEN`(预留 10B 头部),转发时 `(uint8_t*)pk.raw() - PKX_HDR_LEN` 原地填 `pke_len` / `pke_src_id = conv` / `pke_src_addr`,连续内存一把 `hton` + 复用 `Connector::send`
      - 就绪检查:`!is_connected() || !authed()` 时 drop(后端没握手完不发)
      - 错误处理:**后端找不到 / 写失败不踢客户端 KCP session**(send 失败只 log;仅 `get_serv==nullptr` 当非法 dst_id 才 return -1)
- [x] `on_s2c`(backend → KCP 回程,挂在 `on_serv_handle` 的 default 分支)：`get_session(pk_dst_id)` 查 conv → `PK<Host>(pkx.pk(), plen()+PKG_HDR_LEN)` 喂 `kcp Session::send`
      - **路由键约定**:上行 `pk_dst_id`=service_type;后端回包**回填** `pk_dst_id = pke_src_id`(= 网关 stamp 的来源 conv);回程网关据此 `get_session(pk_dst_id)` 找客户端
      - **下行 idem** 由 `kcp Session::send` 内部统一 `next_snd_idem()` stamp(private),转发层不碰 —— 下行加密 IV 唯一性由发送方保证
      - 客户端没了(`s==nullptr`)drop,**不踢后端连接**(与正向对称)
- [x] 单 backend instance 端到端跑通:client → `on_c2s` → 后端 echo_handler → `on_s2c` → client,**echo 已实测通过**(目标地址 + id 当前硬编码 10000)
- [ ] 转发路径剩余硬化项:`get_serv` 路由键 service_type↔instance 语义(多实例,见 2f) / `get_serv==nullptr` 区分"非法 dst_id" vs "后端临时下线"(避免后端抖动批量踢客户端) / `Connector::sbuf_` 背压上限

### 2e. Echo + PING/PONG

- [x] PING / PONG handler + REGIST_REQ / REGIST_RSP 握手已实现
      - 后端 `Proc::on_ping`(回 PONG) / `Proc::on_regist`(set_id + 回 REGIST_RSP)
      - 网关 `kcp::Server::on_pong`(打延迟) / `on_regist_rsp`(置 `Connector::authed`)
      - 心跳判定:Connector `update()` 两级超时(`last_send`/`last_recv`),Proc `check_timeout` 按 `last_recv_ms`
- [x] EchoBackend 进程：[examples/tcp_echo](examples/tcp_echo)(TcpServer + Proc + `regist_handler(1, echo_handler)`,echo_handler 回填 `pk_dst_id = pke_src_id` 后原样回)
- [x] 跑通完整链路：client → KcpServer → `on_c2s` → TcpConnector → TcpServer → echo_handler → `on_s2c` → 反向,**已端到端验证**
- [x] handlers[] 派发机制已验证(后端 `get_handler(1)` 命中 echo_handler);心跳超时清理见上条 `check_timeout`

### 2f. ETCD 服务注册与发现 + REGIST 握手

- [x] **网关侧 control loop 骨架**：typhon::Server 主线程兼 control 线程
      - epoll_wait timeout(`INTERVAL_MS = 10000`,10s) 当定时节拍;stop / 掉线事件即时唤醒
      - **双向事件通道**:主线程→worker 用 `kcp::Server::notify`(SPSC,主线程单生产者);
        worker→主线程用 **pipe**(`notify_serv_disconnected`,低频控制平面,`write ≤ PIPE_BUF` 多生产者天然原子;
        替掉了早期 SPSC —— N 个 worker 并发写会违反单生产者约束)
      - QEvent(Stop/**AddServ**/**RmvServ**/Recv/Send/AddSess/RmvSess),`AddServArg{id, host[32]}`
      - 分发已落地:`update_serv()` 周期 `notify(AddServ)` → kcp::Server `on_new_serv` →
        `Connector::create` → `add_serv`(按 id 去重 `ServMap servs_`,每后端一个 Connector);
        worker 掉线(EPOLLHUP 或心跳判死)经 pipe 回流 `RmvServ` → 主线程从去重集 `servs_` 摘除 →
        下个周期 `update_serv` 重新广播重连
      - **重连风暴防护**:`update_serv` 只在 epoll **超时(n==0)** 跑,不被掉线事件即时驱动 ——
        给重连一个 `INTERVAL_MS` 固定退避(否则 断开→notify→立即重连→又断 会 busy-loop)
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

三层独立、可分别开关:**XDP envelope MAC**(内核态 DoS 过滤) + **X25519 鉴权握手**(每会话密钥协商) + **ChaCha20-Poly1305 AEAD**(payload 加密,机密性+完整性一体)。

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

### 用户侧 ↔ 网关鉴权握手 + 会话密钥协商（已实现）

- [x] **AuthToken（LOGIN 服签发,客户端只搬运不解密）**
      - 字段:`expire(8) / conv(4) / ip(4) / cli_pk[32] / sign[64]`(host 序,无字节序转换)
      - LOGIN 服用**网关 X25519 公钥 sealedbox 加密** + **ed25519 私钥签名**(覆盖 sign 前 48B)
      - 绑 `conv`:token 钉死一个 KCP conv,截获后无法异 conv 重放;`expire` 限时
- [x] **REGIST_REQ / REGIST_RSP 握手**（[on_regist_req](src/kcp/server.cpp)）
      - 网关:sealedbox 解密 → 验 `expire` → 验 `conv == s->conv()` → ed25519 验签 → 派生密钥 → 回 RSP
      - REGIST_REQ_LEN = `PKG_HDR(10) + sizeof(AuthToken)(112) + crypto_box_SEALBYTES(48)` = 170(由 sizeof 推,不写死)
- [x] **会话密钥协商(X25519 ECDH + crypto_kx KDF)**
      - 网关每会话生成**临时 X25519 密钥对**,`x25519_kx_server(tmpsk, token.cli_pk)` 派生双向密钥 `rx/tx`
      - RSP 回包带**网关临时公钥**(明文);客户端 `x25519_kx_client(cli_sk, srv_tmppk)` 派生出对称的 `rx/tx`
      - 底层 libsodium `crypto_kx`:BLAKE2b KDF + 把双方公钥混入,client.tx == server.rx 镜像对齐
- [x] **前向保密(双边)**:网关临时密钥握手后即弃,客户端 X25519 也每次登录新生成
      → 长期密钥(网关静态 sk / LOGIN ed25519 sk / cli_sk)任一泄露都无法回算历史会话密钥
- [x] **加密激活时机 = `Session::authed_`**:握手包(REGIST_REQ/RSP)走明文,authed 翻转后才加解密
      → 网关先 send RSP(明文)再 `set_authed(true)`;客户端收明文 RSP 派生密钥后再置 authed,两端对称
- [x] **cryptor 原语**（[utils/cryptor](include/utils/cryptor.hpp)）:x25519_kx_client / x25519_kx_server
      (crypto_kx 包装) / sealedbox_encrypt/decrypt / ed25519_sign/verify / x25519_keygen / siphash24 / xx20_encrypt / xx20_decrypt(ChaCha20-Poly1305 AEAD)
- [x] test_kcp.py 客户端对齐:纯 Python X25519(RFC 7748) + BLAKE2b KDF,与 libsodium crypto_kx **位等价**;
      12 个预生成写死 token(conv 2000-2011,有效期 10 年)

### payload 加密（ChaCha20-Poly1305 AEAD）

- [x] **只加密 payload**:Package header（10B）明文,只加密 pk_payload,密文尾部附 **16B Poly1305 tag**
      - header 明文让网关读 pk_dst_id 路由 / pk_seq 做 nonce;header 完整性已由 envelope MAC 覆盖
- [x] **per-session 双向密钥**:key 由握手 ECDH 派生(见上),**不再是固定共享 key**
      - 上行用 `tx` / 下行用 `rx`(**各 32B 用满**,不再像 AES-128 截前 16B);每 session key 独立
- [x] **per-packet nonce(12B)**:`[conv 4B][seq 4B][dir 1B][0 3B]`
      - 同 session 内 seq 单调递增保证 (key,nonce) 唯一;dir(0 上行/1 下行)防跨方向复用
      - **conv 撞不再致命**:每 session key 独立,keystream 空间天然隔离
- [x] send 加密(DIR_S2C,tx_key)/ recv 解密(DIR_C2S,rx_key),`authed_` 翻转后才生效;验签失败 / 重放先 drop
- [x] **len_ 不含 tag**:`PK::plen()` 永远是明文 payload 长;tag 只在 wire 瞬间存在(send 末尾附 / recv 开头剥),不进 len_
      - on_s2c 转发回程要先把包拷到独立缓冲再 send(否则加密写 tag 会越界踩 Connector rbuf_ 共享缓冲,已修)
- [x] libsodium 实现(utils/cryptor `xx20_encrypt`/`xx20_decrypt`) + test_kcp.py 客户端对齐
      (ctypes 调 libcrypto `EVP_chacha20_poly1305`,nonce 布局 / 方向 / key 与服务端**位等价**);Unity 端走 BouncyCastle

### 待办 / 已知短板

- [x] ~~**payload 完整性**~~:已换 **ChaCha20-Poly1305 AEAD**(机密性+完整性一体),
      payload bit-flip / 重放被 Poly1305 tag 挡死(原"CTR 可定向篡改不可检测"短板消除)
- [x] ~~**key 复用**(SipHash envelope key 与 AES key 共享 `shkey()`)~~ —— AES payload key 已改为
      **每会话 X25519 ECDH 派生**(见上「鉴权握手」),与 envelope SipHash key(config 固定)分离
      → 剩:envelope MAC key 仍全局静态,可选再上 HKDF + 定时 rotate
- [~] **conv 服务端分配**:per-session ECDH 密钥已消除"conv 撞 → IV 复用"(每 session key 独立);
      但 conv 仍由客户端选 → 仍是 DDoS / conv 抢占面(token 绑 conv 已挡掉**跨 conv 重放**)
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
