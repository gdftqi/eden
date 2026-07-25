# KCP 网关 · MMO / IM 微服务框架

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

一套面向 **MMOARPG / IM** 的服务端框架与配套服务,把「客户端 ↔ 网关 ↔ 后端业务服」这条数据通道做到工业级:**可靠、低延迟、水平扩展、share-nothing 无锁、零分配收包、可观测**。

当前聚焦**网络 IO substrate 与传输安全**;业务层(战斗、AOI、聊天、持久化)由上层接入。

---

## 组件

| 代号 | 语言 | 角色 |
|---|---|---|
| **Adam** | C++20 | 核心框架 `libadam.a` —— KCP 网关库 + TCP 后端库 + 线协议 codec + eBPF + utils。命名空间 `adam` |
| **Moses** | C++ | KCP **网关**服务(基于 Adam 的 kcp 库) |
| **chat** | C++ | 后端 **TCP echo** 示例服务(基于 Adam 的 tcp 库) |
| **Eva** | Go | **登录 / RA 服务** —— 签发 AccessToken、生成会话 conv、从 etcd 读网关注册表返回客户端 |
| **Lilith** | C# | **客户端** —— KCP 核心库(Lilith) + 聊天 GUI(CC,Avalonia) |
| **Ark** | — | **基础设施**编排 —— etcd / redis / mysql 主从 / docker-compose |
| **Zion** | — | 可部署服务的**总目录**(Moses / chat / tools) |

---

## 整体拓扑

```
                              ┌──────────── etcd (服务发现: /moses/<id>) ────────────┐
                              │                                                      │
   ┌────────┐   ①登录 HTTP    ▼          ┌──────────────┐                            ▼
   │ Lilith │ ───────────► ┌──────┐      │    Moses     │ ──TCP──► chat  (后端 echo) │
   │ 客户端 │ ◄─token+conv─ │ Eva  │      │  (KCP 网关)  │ ──TCP──► scene (业务实例)   │
   │  (C#)  │              │ (Go) │      │              │ ──TCP──► guild            │
   └────────┘              └──────┘      └──────────────┘                            │
        │  ②KCP/UDP + 鉴权握手                 ▲                                     │
        └─────────────────────────────────────┘  ③conv 落到负责该 user 的 worker    │
                                                  (sk_reuseport 按 conv%N 分片)      │
   Ark: etcd / redis(conv 序号) / mysql ───────────────────────────────────────────┘
```

1. **登录**:Lilith → Eva(HTTP)。Eva 校验账号 → `MakeConv` 生成 conv → 用**网关公钥 sealedbox 封 + ed25519 签**签发 `AccessToken`(绑 conv/user/expire)→ 从 etcd 读网关列表,一并返回客户端。
2. **建连**:Lilith → Moses(KCP over UDP)。首包 `REGIST_REQ` 携带密封的 token,网关验签后与客户端做 X25519 ECDH,派生每会话双向密钥。
3. **转发**:Moses 按 `data.dst_id` 把上行包转给对应后端(TCP 整帧),后端回程按 `conv` 找回会话下发。

---

## 数据面:线协议

线上**全小端**(x86 / ARM 原生,两端零字节序转换)。内存态 `Package` = `meta` + `data`:

```
Package (内存态)
├─ meta { len:u32, conv:u32, src_addr:u32 }      ← 网关/后端用, 不一定上线
└─ data { id:u32, src_id:u32, dst_id:u32, seq:u32, payload[] }
```

线上宽度:`len`/`id` = **2B**(uint16),其余 = **4B**。`PKG_META_LEN=10` / `PKG_DATA_LEN=14` / `PKG_HDR_LEN=24`。两个方向用不同 wire frame:

```
客户端 → 网关 (KCP, data-only):   meta 由网关本地合成(conv=会话conv, src_addr=对端addr)
  ┌ id 2 ┬ src_id 4 ┬ dst_id 4 ┬ seq 4 ┬ payload ... ┐
  └──────┴──────────┴──────────┴───────┴─────────────┘
  鉴权后 payload 段加密:  [ data头 14B 明文 ][ 密文 ][ ChaCha20-Poly1305 tag 16B ]
                          KCP 消息边界即帧边界, 无需长度字段

网关 → 后端 (TCP, 整帧):          明文(可信内网, 加密在网关卸载)
  ┌ len 2 ┬ conv 4 ┬ src_addr 4 ┬ id 2 ┬ src_id 4 ┬ dst_id 4 ┬ seq 4 ┬ payload ... ┐
  └───────┴────────┴────────────┴──────┴──────────┴──────────┴───────┴─────────────┘
  len = 整帧长, 后端 peek 2B 即可分帧;conv 是路由键, src_addr 让后端拿到来源
```

- `seq`:会话内单调递增(≠0),网关做**幂等 dedup**,兼作加密 nonce 输入
- `dst_id`:目标服务类型(路由键:scene/chat/guild…);`dst_id == 网关自身 id` 时进网关内建处理(REGIST / PING)
- 共享 codec 在 [`Adam/src/core/package.cpp`](Adam/src/core/package.cpp):`data_encode/decode`(客户端段)、`frame_encode/decode`(整帧,自带分帧)、`token_decode`。**codec 不分配内存**——见「零分配收包」。

---

## 网关:控制面 / 数据面分离

- **`kcp::Server`(控制面,主线程)** —— 拥有 XDP `EnvelopeFilter` + `sk_reuseport` `Router`,创建 N 个 Worker、起线程、跑控制面 epoll(服务发现 / 顶号事件),向 etcd 注册。
- **`kcp::Worker`(每线程数据面 actor)** —— 独立 UDP socket(`SO_REUSEPORT`)+ 自己的 epoll + 会话表(按 conv)+ 后端 `Connector` + **无锁 MPSC 事件队列**。
- **`sk_reuseport` + eBPF 按 `conv % nthreads` 路由** —— 同一 conv 恒落同一 Worker,**share-nothing 零锁**;`recvmmsg` 批量收包。
- 会话只按 conv 存(无 user→session 映射);s2c 回程 `conv % N`:本 Worker 直发,否则转 owner Worker(跨线程走 COPY)。

见 [`Adam/include/kcp/server.hpp`](Adam/include/kcp/server.hpp)、[`Adam/src/kcp/worker.cpp`](Adam/src/kcp/worker.cpp)。

## 后端:IO 线程 / Proc 分离

- **`tcp::Server`(IO 线程)** —— accept + 从 socket 读字节,塞进目标 Proc 的 **SPSC 队列 + eventfd 唤醒**。
- **`tcp::Proc`(每线程数据面)** —— 自己的 epoll + eventfd + SPSC + 会话表(按 fd)。同一 `fd` 恒由 `fd % worker_size` 这条 Proc 处理 → **同一会话不跨线程,无锁**。
- 业务以 `server.regist_handler(pkid, handler)` 注册,handler 同步消费。
- **`tcp::Connector`** 是网关侧的后端客户端(连接状态机 + 非阻塞 connect + EPOLLOUT 续发 + PING/PONG 心跳 + 应用层探活)。

见 [`Adam/include/tcp/server.hpp`](Adam/include/tcp/server.hpp)、[`Adam/include/tcp/proc.hpp`](Adam/include/tcp/proc.hpp)。

---

## 传输安全层

客户端 ↔ 网关这一跳三层独立防护:

**① XDP envelope MAC —— 内核态 DoS 过滤**
UDP payload 前 8B 是 SipHash-2-4 tag,覆盖 KCP header。XDP 在网卡驱动层校验,不过直接 `XDP_DROP` —— 伪造 / 垃圾包**不进 socket、不进 KCP、不耗 Worker CPU**。key 走 BPF map(双槽,支持热轮换)。`ikcp.c/.h` 一字不改,envelope 套在 KCP frame 外;sk_reuseport 读 conv 偏移 +8。

**② X25519 ECDH 鉴权握手 —— 每会话密钥协商**
Eva 用网关公钥 **sealedbox 封** + **ed25519 签** 签发 `AccessToken`(116B:`expire/conv/user_id/ip/cli_pk/sign`),客户端只搬运。网关 `REGIST_REQ` 校验链:解封 → 验期 → 验 `conv` → **验 `conv % N == user_id % N` 不变量** → ed25519 验签,再用**临时 X25519** 与 token 内 `cli_pk` 做 `crypto_kx` 派生**双向会话密钥**;`REGIST_RSP` 回带网关临时公钥。两端 X25519 皆临时 → **双边前向保密**。

**③ ChaCha20-Poly1305 AEAD —— payload 加密(机密性 + 完整性一体)**
14B `data` 头**明文**(网关读 `dst_id` 路由 / `seq` 做 nonce),只加密 payload,尾附 **16B Poly1305 tag**。per-packet nonce(12B)= `conv(4) | seq(4) | dir(1) | 0(3)`:`seq` 单调递增保证 (key,nonce) 唯一,`dir` 分上/下行防跨方向复用。基于 libsodium(不依赖 AES-NI,无 AES 旁路 / 降频风险)。

> `conv` 不变量是关键:Eva 的 `MakeConv` 生成的 conv 满足 `conv % N == user_id % N`(N=网关 worker 数,Eva 从 etcd 的 ServerInfo 读取),从而这条会话经 sk_reuseport 恒落到负责该 user 的那个 Worker。

见 [`Adam/src/bpf/envelope.bpf.c`](Adam/src/bpf/envelope.bpf.c)、[`Adam/src/kcp/session.cpp`](Adam/src/kcp/session.cpp)、[`Eva/com/token.go`](Eva/com/token.go)、[`Eva/com/kcp.ex.go`](Eva/com/kcp.ex.go)。

---

## 性能设计

- **零分配收包** —— `recv(Package*)` 解进**调用方提供**的 buffer,内部绝不分配;每线程一块 `alignas(Package) static thread_local` 复用 Package(同线程 decode→消费→下条覆盖),热路径无 per-message malloc/free。仅**跨线程转交**(s2c 跨 Worker)才 COPY 一份(mimalloc)。
- **无锁 MPSC 事件队列**(Vyukov 有界环)+ eventfd / park-unpark 双检唤醒 —— 跨 Worker s2c 转发、服务发现事件走它。
- **share-nothing** —— 网关 by conv、后端 by fd,worker 间物理隔离;时间戳每 worker 自持 monotonic `uint64` ms(免 cross-core cache contention)。
- **`recvmmsg` 批量收包** + **mimalloc** 全局分配器 + `ikcp_allocator` 挂 mimalloc。

见 [收包契约](Adam/src/kcp/session.cpp)、[MPSC](Adam/include/utils/mpsc.hpp)。

## 服务发现 · 登录

- 网关向 etcd 注册 `/{name}/{id:08X}`(如 `/moses/000003E8`),value 为 `ServerInfo` JSON(含 `nthreads`),TTL 续租;掉线经 pipe 回流通知主线程周期重连(`AddServ` / `RmvServ`)。
- Eva 读 `/moses` 前缀拿网关列表返回客户端;`MakeConv` 用 **Redis `INCR`** 取全局序号,`(seq % kmax) * N + user % N` 生成非冲突、满足不变量的 conv。

## 可观测

内嵌 **gperftools CPU profiler**:config `flame: true` 开启,输出 `.prof` → `pprof` 转火焰图(容器内无需 `perf` 权限)。注:on-CPU 采样,看吞吐 / QPS 热点;off-CPU(阻塞 / 延迟)需另配 `perf sched` / eBPF `offcputime`。

---

## 构建与运行

**依赖**:Linux kernel ≥ 5.x(XDP)· C++20(g++ ≥ 11 / clang ≥ 13)· libsodium · mimalloc · spdlog · libbpf + clang(编 BPF)· yaml-cpp · abseil · simdjson · gperftools(profiling)· Go ≥ 1.21(Eva)· etcd / redis(运行期)

```bash
# 1) 核心框架:静态库 + BPF 对象
cd Adam && make                 # → build/libadam.a  build/bpf/{kcp,envelope}.bpf.o

# 2) 基础设施(etcd / redis / mysql)
cd Ark && docker compose up -d

# 3) 登录服 Eva
cd Eva && go build ./...        # 或 docker compose up -d

# 4) KCP 网关 Moses(需 root / CAP_BPF 加载 XDP)
cd Zion/Moses && make           # → moses/ 部署包(二进制 + config + bpf + compose + flame_build.sh)
cd moses && sudo ./moses

# 5) 后端 echo 示例 chat
cd Zion/chat && make && cd chat && ./chat
```

关键配置(`config.yml`):`server.id/name/host` · `etcd.url` · `ifname` + `*_bpf_path`(XDP)· `flame`(profiling)· `log_path` · KCP 调优(`sndbuf/rcvbuf/sndwnd/rcvwnd/nodelay`)· 密钥(`siphash / x25519 / ed25519`)。详见 [CONFIG.md](CONFIG.md)。

---

## 目录结构

```
.
├── Adam/          C++ 框架 (libadam.a): core / kcp / tcp / bpf / utils
├── Eva/           Go 登录 / RA 服务 (token 签发 / MakeConv / etcd 网关注册表)
├── Lilith/        C# 客户端 (Lilith KCP 核心库 + CC 聊天 GUI)
├── Ark/           基础设施 (etcd / redis / mysql 主从 / docker-compose)
├── Zion/          可部署服务总目录
│   ├── Moses/     KCP 网关 (Adam kcp 库)
│   ├── chat/      后端 TCP echo 示例 (Adam tcp 库)
│   └── tools/     ed25519 / x25519 密钥工具
├── docs/          设计文档 (kcp_server / tcp_server / package / login …)
├── CONFIG.md      配置项说明
├── PLAN.md        路线规划
└── README.md
```

---

## 设计哲学

> **底层稳了,业务才跑得动。**

- **share-nothing > 加锁**:网关 by conv、后端 by fd,worker 之间物理隔离
- **零分配热路径**:收包解进复用 buffer,只有跨线程才 COPY
- **明确的所有权契约**:`recv` 只填不分配、buffer 谁调用谁供;并发性 / 线程归属 / 生命周期写进 doxygen
- **fail-fast > silent error**:后端 `ASSERT` 暴露 bug;客户端入口 **reject 不 abort**(不信任对端输入)
- **KCP 协议不动**:envelope MAC 套在 frame 外、加密在 `ikcp_send` 前完成,`ikcp.c/.h` 一字不改,上游升级照单全收
- **全小端 + monotonic `uint64` ms**:两端零字节序转换、免 wrap / 免 cast

---

## 许可

[MIT](LICENSE) © 2026
