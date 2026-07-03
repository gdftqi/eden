# Typhon Code Review 清单

> **图例**：🔴 高危(错=静默损坏/漏洞/判死失灵，重点看) · 🟡 中(逻辑/生命周期) · ⚪️ 稳定(扫一眼) · `☐` 复核完打勾
>
> **用法**：别逐文件顺读——那样每个文件本地看都"对"，会越看越懵。先走下面 **① 端到端场景** 和 **② 跨语言接缝** 两节(90% 的毒 bug 藏在语言/模块的接缝里)，再用各模块表逐条验；每条要在**两端都对上**才打勾。

---

## ① 端到端场景(拿真实字节跟着走)

挑一条真实链路，跟着**同一批字节**穿过三端，比空读强十倍。

| # | 场景 | 走过的模块 | 要盯住的事实 |
|---|---|---|---|
| 1 | **登录换票** | C# `UserLogin`→`HttpSession` ↔ go `user_login`+`http`+`token`+`cryptor` | HTTP kx 方向、`Seal/Open` nonce、`AccessToken` 116B 布局、字段 json tag 对齐 |
| 2 | **KCP 握手 REGIST** | C# `KcpSession.registReq`→`Kcp`→`Crypto` ↔ bpf `envelope_filter`/`router` ↔ C++ `server.on_regist_req`+`session`+`cryptor` | envelope MAC(前 24B)、conv 路由、`REGIST_REQ_LEN=164`、sealedbox 解密、kx(server 端 swap rx/tx) |
| 3 | **首个业务 echo** | C# `Package.encode`+AEAD ↔ C++ `session.recv`+`server.on_c2s` | 线序(大端头)、AEAD nonce `conv\|seq\|dir`、`src_id==user_id`、`dst_id` 路由、seq 去重 |
| 4 | **断线→重连** | C# `Kcp.Update`+`KcpSession`(3 条死路)→`Refresh` ↔ go `refresh`+`token.CheckFromRedis` ↔ C++ `ikcp` RST gate | `DeadReason`(Timeout/Rst/Reset)三路都置位、refreshToken 滚动、uuid 校验、RST 门限不误杀 |

---

## ② 跨语言接缝(单文件看不出的都在这)

| 接缝 | 契约(必须逐字节/逐字段一致) | 两端 | ✓ |
|---|---|---|---|
| 协议行为 | RST cmd(87)、判死、保活 PING/PONG、序号窗口、cmd 范围校验 | C# `Kcp.cs` ↔ C++ `ikcp.c` | ☐ |
| AEAD | nonce = `conv(4)\|seq(4)\|dir(1)\|000`；DIR_C2S=0/S2C=1；key=32B | `Crypto` ↔ `cryptor.hpp` ↔ `cryptor.go` | ☐ |
| envelope MAC | 8B SipHash，覆盖 datagram 前 24B(OVERHEAD)；wire=`[8B MAC][datagram]` | `Crypto`/`Kcp.Output` ↔ bpf `envelope_filter` | ☐ |
| kx(ECDH) | crypto_kx；BLAKE2b(q‖client_pk‖server_pk)；**server 端 swap rx/tx** | `Crypto` ↔ `cryptor.go` | ☐ |
| 线序 | 消息头一律大端；多字节字段 hton/ntoh | C# `Package.cs` ↔ C++ `package.hpp`(`Pke<Host/Net>`) | ☐ |
| 明文握手 | REGIST 字段/长度；`Token` 116B、封装后 164B | C# `KcpSession.Init/registReq` ↔ C++ `on_regist_req` ↔ go `AccessToken` | ☐ |
| HTTP DTO | `hpk/kpk/token/info` + 响应 `conv/host/mac_key/access_token/refresh_token` 的 json tag | C# `UserLogin`/`Refresh` DTO ↔ go `handlers` struct | ☐ |
| conv 路由 | conv=userID+一致性 hash；conv==0 归属 | C# `Init` ↔ bpf `router` ↔ C++ `server` | ☐ |

---

# c++

## bpf 🔴🔴（原 outline 漏了，安全第一道防线，必须看）

### envelope_filter（XDP）🔴🔴
**作用**：内核态 XDP，对入站 UDP 先校验 8B SipHash MAC + 长度，坏包直接 `XDP_DROP`，协议无关。

| 关注点 | 需要检查的点 | ✓ |
|---|---|---|
| MAC 覆盖范围 | 校验的字节区间是否恰为 datagram 前 24B(OVERHEAD)，与 C# `Kcp.Output` 完全一致 | ☐ |
| 长度边界 | 短包/超长包、`len < 8+24` 时是否安全 DROP，无越界读 | ☐ |
| key 来源 | SipHash key 与用户会话 key 的关系、key 轮换/多 key 查表是否正确 | ☐ |
| 放行语义 | PASS 的包是否原样(含 8B MAC)交 userland，由 `on_udp_handle` 偏移 8B 喂 ikcp | ☐ |

### router（sk_reuseport）🔴
**作用**：按 conv 把 UDP 分发到对应 worker socket；conv==0 的兜底路由。

| 关注点 | 需要检查的点 | ✓ |
|---|---|---|
| conv 提取 | 从 datagram 偏移处取 conv 的字节序/偏移是否正确 | ☐ |
| 路由一致性 | conv→worker 的 hash 与网关分配 conv 的规则一致，重连回到同 worker | ☐ |
| conv==0 | 握手/未分配 conv 的包落到哪、会不会打散 | ☐ |

## core

### package 🔴
**作用**：消息协议 v2(10B 头，大端)、`PackageEx`、`Token`、字节序 phantom-type `Pke<Host/Net>`/`Pk`。

| 关注点 | 需要检查的点 | ✓ |
|---|---|---|
| 线序 | 每个头字段的 hton/ntoh 是否唯一入口、与 C# `Package.cs` 逐字段对齐 | ☐ |
| phantom-type | 是否只有 `Host` 能读字段、无裸 `pke_hton`、零开销转换是否被绕过 | ☐ |
| Token 布局 | `Token` 字段/对齐/大小(116B)与 go `AccessToken` 一致；`REGIST_REQ_LEN` 推导 | ☐ |
| payload 边界 | payload 长度可为 0(含 authed 后)是否全链路支持 | ☐ |

### buffer 🟡
**作用**：`SndBuf`/`RcvBuf`——TCP(后端)累积/环形缓冲，半包与 EPOLLOUT 续发。

| 关注点 | 需要检查的点 | ✓ |
|---|---|---|
| 半包 | 读到不完整包时的保留/拼接是否正确、无丢字节 | ☐ |
| 越界 | 写入/扩容边界，长度字段可信度(后端可信，见 tcp 说明) | ☐ |
| 残留续发 | 部分写残留进 `SndBuf`，EPOLLOUT 再 flush 的指针推进 | ☐ |

### error ⚪️
**作用**：全局错误码约定(x* 返回码；系统错误直接 `-errno`)。

| 关注点 | 需要检查的点 | ✓ |
|---|---|---|
| 码段不撞 | pkg(-101xx)/kcp(-102xx)/tcp/conf 各段无重叠、无重复定义 | ☐ |
| 全覆盖 | 每个返回码在调用方都有分支处理，无"吞掉" | ☐ |

### qevent 🟡
**作用**：`QEvent`(RecvArg/AddServArg…)——IO 线程↔主线程的低频控制平面事件。

| 关注点 | 需要检查的点 | ✓ |
|---|---|---|
| 所有权 | 跨队列传递的 Package/指针谁释放、有无 double-free/泄漏 | ☐ |
| 枚举覆盖 | `Type` 每个值主线程都有处理，无漏 case | ☐ |
| 队列语义 | 底层 SPSC 用法(单生产者单消费者)是否成立 | ☐ |

### typhon(typhon.in / typhon.cpp)🟡
**作用**：共享 `State` 枚举/配置(typhon.in)；网关主程序(`typhon.cpp`：`update_serv`、epoll 循环)。

| 关注点 | 需要检查的点 | ✓ |
|---|---|---|
| 重连风暴 | `update_serv` 只在 epoll 超时 `n==0` 跑(已修)，别回退 | ☐ |
| 主循环 | epoll 事件分派、EPOLLOUT 背压、sentinel(成员地址)用法 | ☐ |
| serv 注册 | serv_id/host 注册与服务发现回流(pipe)是否闭环 | ☐ |

## kcp

### ikcp 🔴
**作用**：KCP ARQ 核心(skywind 原生 + typhon 改造：RST/判死/保活)。

| 关注点 | 需要检查的点 | ✓ |
|---|---|---|
| 判死 | `ikcp_update` 返回 <0 两条路(rst=-2 先判、timeout=-1)互斥且优先级对 | ☐ |
| RST 门限 | `registered==0 && !(cmd==PUSH && sn==0)` gate 不误杀握手、正确回 RST | ☐ |
| cmd 校验 | cmd 范围检查(PUSH…RST)、非法 cmd 返回 -3 | ☐ |
| 保活 | 服务端 `ping_active=false` 只回 PONG；收到任意包刷新 `last_rcv_ms` | ☐ |
| 删字段 | `state`/`dead_link` 已删干净，无残引用 | ☐ |

### session 🔴
**作用**：每 conv 会话——`recv()` 校验、authed 门、AEAD 解密、`set_key`/`set_user_id`。

| 关注点 | 需要检查的点 | ✓ |
|---|---|---|
| 入包校验 | 长度、`id/seq/dst_id/src_id != 0`、`rcv_req_>=seq→xDUP` 顺序与边界 | ☐ |
| authed 门 | 未鉴权只放行握手；authed 后 `src_id != user_id_ → xERR_PKT_SRC` | ☐ |
| AEAD | 解密失败 `xERR_PK_DEC`；nonce 拼装与 C#/go 一致；空 payload 放行 | ☐ |
| 生命周期 | `set_output` 在 ctor 已设(RST 触发 output 不空)、`authed()=user_id_>0` | ☐ |

### server 🔴
**作用**：KCP 网关 server——`on_udp_handle`、sessions 表、RST、`on_c2s` 路由、`on_regist_req`。

| 关注点 | 需要检查的点 | ✓ |
|---|---|---|
| new-session/RST | 无 session+非握手→回 RST 并 `remove_session`；`is_new` 分支不反 | ☐ |
| add_session 竞态 | `emplace` 返回值判定、`on_connected` 失败回滚 erase、不每包 add | ☐ |
| on_c2s | `!authed→xERR_NOT_AUTH`、`get_serv(dst_id)==null→xERR_PKT_DST`、未连接→xOK | ☐ |
| on_regist_req | `in.plen()!=164→xERR_PK_LEN`、sealedbox 解密、expire、set_key/user_id、users_ | ☐ |
| 错误处理 | `res<0` 时 remove+break 是否漏清理；临时诊断日志(308)是否已删 | ☐ |

### config ⚪️
**作用**：网关 KCP 配置加载(yaml)。

| 关注点 | 需要检查的点 | ✓ |
|---|---|---|
| 校验 | MAC key 长度、timeout/窗口/mtu 默认值与合法性 | ☐ |
| 缺省 | 缺字段时 fail-fast 还是静默默认 | ☐ |

## tcp（后端连接层，仅接可信网关；ASSERT-on-malformed 合理）

### server 🟡
**作用**：后端侧 `tcp::Server`——accept 网关连接(不暴露给客户端)。

| 关注点 | 需要检查的点 | ✓ |
|---|---|---|
| 信任边界 | 只接网关、无远程攻击者，ASSERT 处理畸形包是否确实都在此边界内 | ☐ |
| 连接管理 | accept/关闭/半开清理、fd 泄漏 | ☐ |

### connector 🟡
**作用**：网关→后端的出站连接(hton 发包、半写 `sbuf_` 等 EPOLLOUT 续发、探活)。

| 关注点 | 需要检查的点 | ✓ |
|---|---|---|
| 探活 | 应用层 PING/PONG + 两级超时(last_send/last_recv)；判死即摘+通知主线程 | ☐ |
| 半写续发 | 部分写残留/EPOLLOUT 续发指针；背压 | ☐ |
| 不自重连 | Connector 自身不重连(重连在服务发现层)是否被误加 | ☐ |
| 线序 | 发送 hton、`decode` 内 ntoh 后为 host 序 | ☐ |

### session 🟡
**作用**：后端 TCP 会话(`TcpSession`)——收发缓冲、半包续发。

| 关注点 | 需要检查的点 | ✓ |
|---|---|---|
| 帧解析 | 长度前缀/半包、`xAGAIN` 语义 | ☐ |
| 续发 | `sbuf_` 残留 flush | ☐ |

### proc 🟡
**作用**：后端包处理(`Proc`)——路由/分发到业务。

| 关注点 | 需要检查的点 | ✓ |
|---|---|---|
| 分发 | 包类型→处理映射齐全、未知类型处理 | ☐ |
| 与 server 协作 | 生命周期/回调所有权 | ☐ |

### config ⚪️
**作用**：后端 TCP 配置。

| 关注点 | 需要检查的点 | ✓ |
|---|---|---|
| 校验 | 端口/超时/缓冲默认值合法 | ☐ |

## utils

### cryptor 🔴
**作用**：SipHash(MAC)、crypto_kx、ChaCha20-Poly1305 AEAD、sealedbox。

| 关注点 | 需要检查的点 | ✓ |
|---|---|---|
| AEAD | nonce 拼法/长度、tag 16B、加解密对称、与 C#/go 逐字节 | ☐ |
| MAC | SipHash key 长度、tag 8B、覆盖范围与 bpf/C# 一致 | ☐ |
| kx | server/client 各自 rx/tx 方向(server swap)、失败返回 | ☐ |
| sealedbox | 匿名封装/解封的 pk/sk 用法正确 | ☐ |

### spsc ⚪️
**作用**：单生产者单消费者无锁队列。

| 关注点 | 需要检查的点 | ✓ |
|---|---|---|
| 内存序 | `cached_head_` 优化、acquire/release 配对、无 ABA | ☐ |
| 容量 | 满/空判定、2 的幂掩码 | ☐ |

### obj_pool ⚪️
**作用**：对象池(复用，减分配)。

| 关注点 | 需要检查的点 | ✓ |
|---|---|---|
| 配平 | 借/还成对、无泄漏/double-return | ☐ |
| 线程性 | 是否单线程假设、跨线程用是否安全 | ☐ |

### timing_wheel ⚪️（已写未接入）
**作用**：时间轮(超时类一次性稀疏事件)。

| 关注点 | 需要检查的点 | ✓ |
|---|---|---|
| 定位 | 别用于 ikcp 周期 tick；接入前置是 `ikcp_check` | ☐ |
| 正确性 | 槽位/进位、Handle 失效/取消 | ☐ |

### sys ⚪️
**作用**：系统封装(文件锁 flock、时间 timespec…)。

| 关注点 | 需要检查的点 | ✓ |
|---|---|---|
| 错误 | 系统调用返回值/EINTR 处理 | ☐ |

### string_ex ⚪️
**作用**：字符串小工具。

| 关注点 | 需要检查的点 | ✓ |
|---|---|---|
| 边界 | 空串/越界/编码 | ☐ |

---

# c#（lilith 协议库；GUI CC/ 不在本轮范围）

## Core

### KcpSession 🔴
**作用**：客户端会话——3 线程(主/ioRecv/ioSend)、判死→`DeadReason`、重连编排、握手。

| 关注点 | 需要检查的点 | ✓ |
|---|---|---|
| 判死三路 | Timeout/Rst(sndLoop `Update<0`)、Reset(socket 异常，`if(Running)`)都置位 | ☐ |
| 线程/状态 | `running` CAS、teardown 里 `Join` 后再 `OnDisconnected`(内存可见性)、二次 Connect 干净起点 | ☐ |
| 重连 | 30s 预算、`reconnecting` 防重入、`connectTsk` 只 set 一次、无半开会话泄漏 | ☐ |
| Init | 重置 `DeadReason/authed/seq`；`macKey`/token 解码 | ☐ |
| 资源 | `Package.Pool` 借还、socket/线程释放、队列 Clear | ☐ |

### Package 🔴
**作用**：C++ package 的 C# 镜像(10B 头，大端)。

| 关注点 | 需要检查的点 | ✓ |
|---|---|---|
| 线序 | 每字段 encode/decode 与 `package.hpp` 逐字节对齐(大端) | ☐ |
| 边界 | payload 长度(可 0)、越界、池化对象复用不脏 | ☐ |

## Kcp

### Kcp 🔴
**作用**：ikcp 的 C# 移植 + 内建 SipHash + RST/超时/PING。

| 关注点 | 需要检查的点 | ✓ |
|---|---|---|
| 与 ikcp 对齐 | RST(87)、判死返回码、cmd 分派、序号窗口行为一致(最易 drift) | ☐ |
| MAC | `Output` 拼 8B MAC 覆盖前 24B、`Input` 剥 8B，与 C++/bpf 一致 | ☐ |
| 保活 | `ping_active` 客户端发 PING、`last_rcv_ms`/`last_snd_ms` 刷新点 | ☐ |
| 判死出口 | `Update` 里 `rst`→-2、timeout→-1，首次 update 初始化 last_rcv 防误判 | ☐ |

### Utils 🟡
**作用**：encode/decode 位操作(LE 助手，KCP 头用)。

| 关注点 | 需要检查的点 | ✓ |
|---|---|---|
| 一致 | 8/16/32 位 en/decode 与 ikcp `ikcp_encode*` 完全一致(LE) | ☐ |

### Segment / Pool / AckItem ⚪️
**作用**：KCP 段、非线程安全对象池、ACK 记录。

| 关注点 | 需要检查的点 | ✓ |
|---|---|---|
| Pool | 单线程假设是否被 SafeKcp 串行化保证 | ☐ |
| Segment | 字段 encode 与 C++ seg 头一致 | ☐ |

## Tools

### Crypto 🔴
**作用**：X25519 kx、ChaCha20-Poly1305、SipHash、随机 nonce、base64。

| 关注点 | 需要检查的点 | ✓ |
|---|---|---|
| 逐字节 | AEAD/MAC/kx 与 C++ `cryptor`、go `cryptor` 三端一致(有 `cryptor_test` 交叉验) | ☐ |
| kx | `X25519KxClient` 入参(cliSk/cliPk/srvPk)、rx/tx 方向 | ☐ |
| nonce | `RandomNonce` 长度/随机源；AEAD nonce 拼装 | ☐ |

### HttpSession 🔴
**作用**：登录信道——kx 建 rx/tx、`Seal`/`Open`、`PostSecureAsync`。

| 关注点 | 需要检查的点 | ✓ |
|---|---|---|
| 信道复用 | rx/tx 在 `Init` 建一次、login 与 refresh 共用；与 go 端 kx 一致 | ☐ |
| Seal/Open | `base64(nonce‖cipher+tag)`、tx 加密 rx 解密、长度下界校验 | ☐ |
| PostSecure | 请求原样 JSON(不 Seal)、只 Open 响应 `data`；`Code!=0` 抛错 | ☐ |

### SafeKcp 🟡
**作用**：串行化 KCP 访问(收 ACK 改发送缓冲需互斥)、透传判死信号。

| 关注点 | 需要检查的点 | ✓ |
|---|---|---|
| 锁覆盖 | 所有 ikcp 调用(Input/Receive/Send/Update/Check)都在锁内、无遗漏 | ☐ |
| 透传 | `Update` 判死返回码原样上抛 | ☐ |

### BlockingQueue / SafePool ⚪️
**作用**：阻塞队列、线程安全对象池。

| 关注点 | 需要检查的点 | ✓ |
|---|---|---|
| 并发 | 唤醒/信号、Clear 时机、借还配平 | ☐ |

---

# go（RA / 登录鉴权，原 outline 空缺，补全）

## com

### token 🔴
**作用**：`AccessToken`(签发，116B/封装 164B)、`RefreshToken`(对称加解密 + redis 撤销)。

| 关注点 | 需要检查的点 | ✓ |
|---|---|---|
| AccessToken | 字段布局/大小与 C++ `Token` 一致；`SealeaBoxAndSign` 用 ed25519_sk + x25519_pk | ☐ |
| RefreshToken 密钥 | 用**专用 `RefreshKey`**(非 selfSk)对称加密、key 分离 | ☐ |
| Encrypt/Decrypt | `base64(nonce‖cipher)`、XX20、失败返回 | ☐ |
| 撤销 | `UpdateToRedis`(30d 滑动 TTL)、`CheckFromRedis` 用 `!bytes.Equal`；**`redis.Nil` 死分支应改 `len(uid)==0`** | ☐ |

### redis.obj ⚪️
**作用**：`UserSession`(conv/host 等)落 redis。

| 关注点 | 需要检查的点 | ✓ |
|---|---|---|
| 键/TTL | key 命名、过期、序列化格式 | ☐ |

## handlers

### user_login 🔴
**作用**：`/user_login`——校验凭据、kx、签 AccessToken、发 refreshToken。

| 关注点 | 需要检查的点 | ✓ |
|---|---|---|
| DTO | `hpk/kpk/info` 入、`conv/host/mac_key/access_token/refresh_token` 出，tag 对齐 C# | ☐ |
| 占位 | 真凭据校验、真实 conv/user_id(当前是占位)、Host/HostID 硬编码 | ☐ |
| refresh | 生成 uuid→RefreshToken→`UpdateToRedis`→Encrypt | ☐ |

### refresh 🔴
**作用**：`/refresh`——验 refreshToken、kx、重签、**滚动 uuid**。

| 关注点 | 需要检查的点 | ✓ |
|---|---|---|
| 解密链 | `Decrypt(RefreshKey)`→`CheckFromRedis`→kx→AccessToken | ☐ |
| 轮换 | Step 7 换新 uuid→Encrypt→回包；旧 refreshToken 失效 | ☐ |
| DTO | 请求 `hpk/kpk/token`、响应同 login，tag 对齐 C# `Refresh` | ☐ |

### regist_user ⚪️
**作用**：注册。

| 关注点 | 需要检查的点 | ✓ |
|---|---|---|
| 校验 | 用户名/密码规则、重复注册、写库 | ☐ |

## utils

### cryptor 🔴
**作用**：kx/AEAD/sealedbox 的 go 实现(与 C++/C# 对拍)。

| 关注点 | 需要检查的点 | ✓ |
|---|---|---|
| 三端一致 | `X25519KxServer` swap rx/tx、`XX20Encrypt/Decrypt` nonce/aad、sealedbox | ☐ |
| 测试 | `cryptor_test` 交叉验覆盖度(TokenCrossCheck) | ☐ |

### http 🔴
**作用**：`Seal`/`Open`/`WebResponse`——HTTP 加密信道封装。

| 关注点 | 需要检查的点 | ✓ |
|---|---|---|
| Seal/Open | 与 C# `HttpSession` 逐字节(nonce‖cipher、key 方向) | ☐ |
| 响应包 | `WebResponse` 的 `code/error/data` 信封形状与 C# `PostSecureAsync` 期望一致 | ☐ |

### string.ex ⚪️
**作用**：`ToJSON` 等。

| 关注点 | 需要检查的点 | ✓ |
|---|---|---|
| 序列化 | 与传输格式一致 | ☐ |

## conf / mid（基础设施，功能性扫一眼）

### config / redis / etcd / mysql / rabbit ⚪️
**作用**：配置加载、各中间件封装。

| 关注点 | 需要检查的点 | ✓ |
|---|---|---|
| Init | 连接失败 fail-fast、超时/池配置 | ☐ |
| redis | `RedisGet` 吞 `redis.Nil` 返回 `("",nil)` 的语义(影响 token 校验) | ☐ |
| etcd | 服务发现接入(下一步)前先确认 lease/watch 封装正确 | ☐ |
