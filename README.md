# KCP 网关 · MMO / IM 微服务框架

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

一套面向 **MMOARPG / IM** 的服务端框架与配套服务,把「客户端 ↔ 网关 ↔ 后端业务服」这条数据通道做到工业级:**可靠、低延迟、水平扩展、share-nothing 无锁、零分配收包、可观测**。

当前聚焦**网络 IO substrate 与传输安全**;业务层(战斗、AOI、聊天、持久化)由上层接入。

> **本分支只含基础框架**,不含任何具体产品的业务代码。框架改动一律在 `basic` 上做,产品分支从这里合入。

---

## 组件

| 代号 | 语言 | 角色 |
|---|---|---|
| **Adam** | C++20 | 核心框架 `libadam.a` —— KCP 网关库 + TCP 后端库 + 线协议 codec + eBPF + Scylla 访问层 + utils。命名空间 `adam` |
| **Moses** | C++ | KCP **网关**服务(基于 Adam 的 kcp 库) |
| **Noah** | C++ | **终端路由服务**(`router: true`)—— 受理终端进入/离开、顶号仲裁 |
| **Eva** | Go | **登录 / RA 库** —— 签发 AccessToken、生成会话 conv、从 etcd 读网关注册表返回客户端;兼管账号(`t_user_basic`)与 S3 上传。**不带 main**,由下面的 Adah 装配 |
| **Adah** | Go | **登录服务** —— Eva 的可部署入口,本身零业务逻辑,只是 `boot.Init/NewEngine/Run` 三步 |
| **Lilith** | C# | **客户端 KCP 核心库** —— 会话端点 + 3 线程泵 + 重连 |
| **Ark** | — | **基础设施**编排 —— etcd / redis / mysql 主从 / scylla / docker-compose |
| **Zion** | — | 可部署服务的**总目录**(Moses / Noah / Adah) |

---

## 整体拓扑

```
                              ┌──────────── etcd (服务发现: /moses/<id>) ────────────┐
                              │                                                      │
   ┌────────┐   ①登录 HTTP    ▼          ┌──────────────┐                            ▼
   │ Lilith │ ───────────► ┌──────┐      │    Moses     │ ──TCP──► Noah  (终端路由)  │
   │ 客户端 │ ◄─token+conv─ │ Eva  │      │  (KCP 网关)  │ ──TCP──► 业务后端 A        │
   │  (C#)  │              │ (Go) │      │              │ ──TCP──► 业务后端 B        │
   └────────┘              └──────┘      └──────────────┘                            │
        │  ②KCP/UDP + 鉴权握手                 ▲                                     │
        └─────────────────────────────────────┘  ③conv 落到负责该 user 的 worker    │
                                                  (sk_reuseport 按 conv%N 分片)      │
   Ark: etcd / redis(conv 序号) / mysql ───────────────────────────────────────────┘
```

1. **登录**:Lilith → Eva(HTTP)。Eva 校验账号 → `MakeConv` 生成 conv → 用**网关公钥 sealedbox 封 + ed25519 签**签发 `AccessToken`(绑 conv/user/expire)→ 从 etcd 读网关列表,一并返回客户端。
2. **建连**:Lilith → Moses(KCP over UDP)。首包 `REGIST_REQ` 携带密封的 token,网关验签后与客户端做 X25519 ECDH,派生每会话双向密钥。
3. **登记**:鉴权通过后网关向 `router: true` 的服务(Noah)发 `TER_ENT_REQ` 登记该终端 —— 顶号仲裁与下线通知的前提。
4. **转发**:Moses 按 `data.dst_id` 把上行包转给对应后端(TCP 整帧),后端回程按 `conv` 找回会话下发。目标未就绪时网关回 `PID_TER_ERROR`,**不断开连接**。

---

## 数据面:线协议

线上**全小端**(x86 / ARM 原生,两端零字节序转换)。内存态 `Package` = `meta` + `data`:

```
Package (内存态)
├─ meta { len:u32, conv:u32, src_addr:u32 }      ← 网关/后端用, 不上线(网关本地合成)
└─ data { pid:u32, src_id:u32, dst_id:u32, payload[] }
```

线上宽度:`len`/`pid` = **2B**(uint16),其余 = **4B**。`PKG_META_LEN=10` / `PKG_DATA_LEN=10` / `PKG_HDR_LEN=20`。两个方向用不同 wire frame:

```
客户端 ↔ 网关 (KCP, data-only):   meta 由网关本地合成(conv=会话conv, src_addr=对端addr)
  ┌ pid 2 ┬ src_id 4 ┬ dst_id 4 ┬ payload ... ┐
  └───────┴──────────┴──────────┴─────────────┘
  这一层不加密 —— 加密在下面的「信封层」, 包住整个 KCP 数据报
  KCP 消息边界即帧边界, 无需长度字段

网关 → 后端 (TCP, 整帧):          明文(可信内网, 加密在网关卸载)
  ┌ len 2 ┬ conv 4 ┬ src_addr 4 ┬ pid 2 ┬ src_id 4 ┬ dst_id 4 ┬ payload ... ┐
  └───────┴────────┴────────────┴───────┴──────────┴──────────┴─────────────┘
  len = 整帧长, 后端 peek 2B 即可分帧;conv 是路由键, src_addr 让后端拿到来源
```

- `dst_id`:目标服务类型(路由键:scene/chat/guild…);`dst_id == 网关自身 id` 时进网关内建处理(REGIST / PING)
- **无幂等序号** —— KCP 的 `ikcp_parse_data` 自带 sn 去重,重复段只回 ACK 不入 `rcv_queue`,应用层不需要再判重
- 共享 codec 在 [`Adam/src/core/package.cpp`](Adam/src/core/package.cpp):`data_encode/decode`(客户端段)、`frame_encode/decode`(整帧,自带分帧)。**codec 不分配内存** —— 见「零分配收包」。

---

## 网关:控制面 / 数据面分离

- **`kcp::Server`(控制面,主线程)** —— 拥有 XDP `EnvelopeFilter` + `sk_reuseport` `Router`,创建 N 个 Worker、起线程、跑控制面 epoll(服务发现 / 顶号事件),向 etcd 注册。
- **`kcp::Worker`(每线程数据面 actor)** —— 独立 UDP socket(`SO_REUSEPORT`)+ 自己的 epoll + 会话表(按 conv)+ 后端 `Connector` + **无锁 MPSC 事件队列**。
- **`sk_reuseport` + eBPF 按 `conv % nthreads` 路由** —— 同一 conv 恒落同一 Worker,**share-nothing 零锁**;`recvmmsg` 批量收包。
- 会话只按 conv 存(无 user→session 映射);s2c 回程 `conv % N`:本 Worker 直发,否则转 owner Worker(跨线程走 COPY)。

见 [`Adam/include/kcp/server.hpp`](Adam/include/kcp/server.hpp)、[`Adam/src/kcp/worker.cpp`](Adam/src/kcp/worker.cpp)。

## 后端:IO 线程 / Reactor 分离

- **`tcp::Server`(IO 线程)** —— accept + 从 socket 读字节,塞进目标 Reactor 的**事件队列 + eventfd 唤醒**。
- **`tcp::Reactor`(每线程数据面)** —— 自己的 epoll + eventfd + 会话表(按 fd)+ 终端表(按 uid)。同一 `fd` 恒由同一条 Reactor 处理 → **同一会话不跨线程,无锁**;跨 Reactor 的顶号经 `Directory` 仲裁后走消息队列。
- 业务以 `server.regist_handler(pid, handler)` 注册,handler 同步消费。
- **`tcp::Connector`** 是网关侧的后端客户端(连接状态机 + 非阻塞 connect + EPOLLOUT 续发 + PING/PONG 心跳 + 应用层探活)。

见 [`Adam/include/tcp/server.hpp`](Adam/include/tcp/server.hpp)、[`Adam/include/tcp/reactor.hpp`](Adam/include/tcp/reactor.hpp)。

---

## 传输安全层

客户端 ↔ 网关这一跳采用 **DTLS / QUIC / WireGuard 的主流形态**:安全层在 KCP **之外**,整个 KCP 数据报被 AEAD 包住。

```
UDP 载荷布局
偏移  0            8        12       16                        末尾-16
     ┌─槽位 MAC─┬─ conv ─┬─计数器─┬── AEAD(整个 KCP 数据报) ──┬─ tag ─┐
     │   8B     │   4B   │  4B    │   明文 = 24B KCP头 + 数据  │  16B  │
     └──────────┴────────┴────────┴───────────────────────────┴───────┘
                 └────── AAD(认证但不加密) ──────┘
```

开销 32B/包,`KCP_MTU` = 1450 − 32 = **1418**。握手期(密钥未就绪)格式为 `[8B MAC][裸 KCP 数据报]`,**槽位 MAC 任何时候都要盖**,否则 XDP 会丢光整个握手。

**① XDP 槽位 MAC —— 内核态 DoS 过滤**
前 8B 是 SipHash-2-4 tag,覆盖密文的前 24B。XDP 在网卡驱动层校验,不过直接 `XDP_DROP` —— 伪造 / 垃圾包**不进 socket、不进 KCP、不耗 Worker CPU**。key 按 `conv & (nkeys-1)` 分槽共享(N=256),走 BPF map 支持热轮换。
**这是 DoS 过滤,不是会话级认证** —— 同槽位的客户端能互相算出合法 MAC,真正的认证在 ②③。
同时做**新会话限速**:`conv` 不在 `active_conv` 集合里的包才计入每源 IP 的 1 秒窗口,已建立会话一律放行(避免误伤 NAT 后的一片玩家)。

**② X25519 ECDH 鉴权握手 —— 每会话密钥协商**
Eva 用网关公钥 **sealedbox 封** + **ed25519 签** 签发 `AccessToken`(116B:`expire/conv/user_id/ip/cli_pk/sign`),客户端只搬运。网关 `REGIST_REQ` 校验链:解封 → 验期 → 验 `conv == s->conv()` → 验 `token.user_id == data.src_id` → ed25519 验签,再用**临时 X25519** 与 token 内 `cli_pk` 做 `crypto_kx` 派生**双向会话密钥**;`REGIST_RSP` 回带网关临时公钥。两端 X25519 皆临时 → **双边前向保密**。

**③ ChaCha20-Poly1305 信封 —— 包住整个 KCP 数据报**
`conv` 与计数器放在 AAD 里(**认证但不加密**,因为解密前就要用它们定位会话)。计数器每次**发送**递增(含重传与纯 ACK),兼作 AEAD nonce 与防重放序号;耗尽即拆会话。收端用 **RFC 6479 式 64 位滑动窗口**去重:容忍乱序(KCP 依赖它),拒绝重放。

> **为什么要下沉。** 改造前 AEAD 在 KCP 之上,KCP 头裸露在外,下面只有分槽共享的 MAC —— 同槽客户端可伪造受害者的 `una`/`rmt_wnd`/`sn`,一个包就能清空对方发送缓冲或劫持下行。根因是**认证发生在错误的层**:攻击面在 `ikcp_input` 里,而认证在它之后。下沉之后这四条注入路径全部由构造消失。

> `conv` 不变量是关键:Eva 的 `MakeConv` 生成的 conv 满足 `conv % N == user_id % N`(N=网关 worker 数,Eva 从 etcd 的 ServerInfo 读取),从而这条会话经 sk_reuseport 恒落到负责该 user 的那个 Worker。**网关不单独校验这条不变量** —— 它由构造成立:conv 只可能来自 Eva,而 token 又把 conv 钉死(`token.conv == 会话 conv`),伪造一个 `% N` 不匹配的 conv 拿不到能通过验签的 token。`conv` 放在偏移 8 是刻意的 —— 与改造前 KCP 头里 conv 的位置重合,两个 BPF 程序一个字节都不用改。

见 [`Adam/src/bpf/envelope.bpf.c`](Adam/src/bpf/envelope.bpf.c)、[`Adam/src/kcp/session.cpp`](Adam/src/kcp/session.cpp)、[`Eva/com/token.go`](Eva/com/token.go)。

---

## 错误码:四层号段

规矩是**层越底,位数越少**,段与段不重叠 —— 任何一个码都能反查出它属于哪一层。

| 层 | 号段 | 前缀 | 分组 |
|---|---|---|---|
| 系统 | `-1 ~ -133` | `-errno` 直接透传 | Linux errno 上限 133 |
| ikcp | `-12 ~ -61` | `IKCP_ERR_` | 十位分函数 |
| 框架内部 | `-200 ~ -703` | `x` | 百位分子系统 |
| 服务之间 | `1000 ~ 9999` | `SERR_` | 千位分框架/业务 |
| 对客户端 | `10000+` | `PERR_TER_` / `PERR_REQ_` | 万位分表,千位分框架/业务 |

- **`-1xx` 段刻意留空** —— 与 errno 重叠,而 BPF 加载器同一条调用链上既返回 `-errno` 又返回 `xERR_BPF_*`。
- **`xAGAIN = -EAGAIN`**,与内核同义。
- **`str_error(int)` 是唯一入口**,按号段自动分派(两位数转给 `ikcp_error()`)。兜底能区分「业务自定义 / 框架段但没同步 / 根本不在任何段」,最后一种可抓出跨层误传。
- `PERR_*` 上线,**改动必须同步 C++ / C#**(C# 侧见 [`Lilith/Lilith/Core/Package.cs`](Lilith/Lilith/Core/Package.cs) 的 `ErrorText`)。

见 [`Adam/include/core/error.hpp`](Adam/include/core/error.hpp)。

---

## 性能设计

- **零分配收包** —— `recv(Package*)` 解进**调用方提供**的 buffer,内部绝不分配;每线程一块 `alignas(Package) static thread_local` 复用 Package(同线程 decode→消费→下条覆盖),热路径无 per-message malloc/free。仅**跨线程转交**(s2c 跨 Worker)才 COPY 一份(mimalloc)。
- **无锁 MPSC 事件队列**(Vyukov 有界环)+ eventfd / park-unpark 双检唤醒 —— 跨 Worker s2c 转发、服务发现事件走它。
- **share-nothing** —— 网关 by conv、后端 by fd,worker 间物理隔离;时间戳每 worker 自持 monotonic `uint64` ms(免 cross-core cache contention)。
- **`recvmmsg` 批量收包** + **mimalloc** 全局分配器 + `ikcp_allocator` 挂 mimalloc。

见 [收包契约](Adam/src/kcp/session.cpp)、[MPSC](Adam/include/utils/mpsc.hpp)。

## 服务发现 · 登录

- 网关向 etcd 注册 `/{name}/{id:08X}`(如 `/moses/000003E8`),value 为 `ServerInfo` JSON(含 `nthreads`、`router`、受理的 PID 集合),TTL 续租;掉线经 pipe 回流通知主线程周期重连(`AddServ` / `RmvServ`)。
- Eva 读 `/moses` 前缀拿网关列表返回客户端;`MakeConv` 用 **Redis `INCR`** 取全局序号,`(seq % kmax) * N + user % N` 生成非冲突、满足不变量的 conv。
- 登录限速:**成功**按账号计(攻击者用自己的账号,IP 可轮换),**失败**按 IP+账号计(避免只按账号导致的锁定式 DoS)。

## 可观测

内嵌 **gperftools CPU profiler**:config `flame: true` 开启,输出 `.prof` → `pprof` 转火焰图(容器内无需 `perf` 权限)。注:on-CPU 采样,看吞吐 / QPS 热点;off-CPU(阻塞 / 延迟)需另配 `perf sched` / eBPF `offcputime`。

---

## 构建与运行

**依赖**:Linux kernel ≥ 5.x(XDP)· C++20(g++ ≥ 11 / clang ≥ 13)· libsodium · mimalloc · spdlog · libbpf + clang(编 BPF)· yaml-cpp · abseil · simdjson · cpp-rust-driver(Scylla)· gperftools(profiling)· Go ≥ 1.21(Eva)· .NET(Lilith)· etcd / redis(运行期)

> 上述 C/C++ 依赖**全部被打进 `libadam.a`**(含 mimalloc 的 new/delete 覆盖与 Scylla 静态驱动),下游服务链一个 `libadam.a` 即可,不必逐个再链。

```bash
# 1) 核心框架:静态库 + BPF 对象
cd Adam && make                 # → build/libadam.a  build/bpf/{kcp,envelope}.bpf.o

# 2) 基础设施(etcd / redis / mysql)
cd Ark && docker compose up -d

# 3) 登录服 Adah(Eva 库的可部署入口)
cd Zion/Adah && go build .
./Adah                          # 首次启动自动生成 config.yml + 密钥

# 4) KCP 网关 Moses(需 root / CAP_BPF 加载 XDP)
cd Zion/Moses && make           # → moses/ 部署包(二进制 + config + bpf + compose + flame_build.sh)
cd moses && sudo ./moses

# 5) 终端路由服务 Noah
cd Zion/Noah && make && cd noah && ./noah
```

Eva 是**库**不是可执行程序,`Zion/Adah` 就是它唯一的装配体 —— [Adah/main.go](Zion/Adah/main.go) 全文只有三行。产品要加自己的接口,照这个样子写一个自己的 main,在 `NewEngine()` 和 `Run()` 之间挂路由即可,**不必改 Eva 一行**:

```go
func main() {
    boot.Init("config.yml")     // 配置(缺就从 config.yml.example 生成) → redis → etcd → mysql → s3
    eng := boot.NewEngine()     // 框架路由: user_login / refresh / update / update_user / create_user / upload
    eng.POST("/xxx", XxxHandler)
    boot.Run(eng)
}
```

> **密钥不进版本库。** 版本库里只有 `config.yml.example`,`ed25519_sk` / `self_pk` / `self_sk` / `refresh_key` 四项**留空**。首次启动 `boot.Init` 会拷出 `config.yml`(已被 `.gitignore` 挡住)、生成密钥、写回文件并把权限改 `0600`,同时在日志里打出两个要手工同步的公钥:**ed25519 公钥填进网关的 `ed25519_pk`,`self_pk` 内置进客户端**。已经生成过就别再清空 —— 这两个公钥半边在网关和客户端手里,重生成会让所有登录验签失败。

关键配置(`config.yml`):`server.id/name/host/router` · `etcd.url` · `ifname` + `*_bpf_path`(XDP)· `newsess_max`(新会话限速)· `flame`(profiling)· `log_path` · KCP 调优(`sndbuf/rcvbuf/sndwnd/rcvwnd/nodelay`)· 密钥(`siphash / x25519 / ed25519`)。详见 [CONFIG.md](CONFIG.md)。

> ⚠️ `ifname` 默认是 `lo`(开发用)。**上线前必须改成实际网卡** —— 否则 XDP 挂在 loopback 上,外部流量根本不经过它,槽位 MAC 过滤和新会话限速会**静默失效**(安全性不受影响,用户态是权威,但防护是空的)。

---

## 目录结构

```
.
├── Adam/          C++ 框架 (libadam.a): core / kcp / tcp / bpf / db / utils
├── Eva/           Go 登录 / RA 服务 (token 签发 / MakeConv / etcd 网关注册表 / 限速 / 账号 / 上传)
│   └── boot/      启动序列 (Init / NewEngine / Run) —— 产品自带 main, 挂自己的路由
├── Lilith/        C# 客户端 KCP 核心库
├── Ark/           基础设施 (etcd / redis / mysql 主从 / scylla / docker-compose)
├── Zion/          可部署服务总目录
│   ├── Moses/     KCP 网关 (Adam kcp 库)
│   ├── Noah/      终端路由服务 (Adam tcp 库)
│   └── Adah/      登录服务 (Eva 库的可部署入口)
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
- **认证要发生在攻击面所在的那一层**:安全层包住整个 KCP 数据报,而不是坐在它上面
- **fail-fast > silent error**:后端 `ASSERT` 暴露 bug(内网可信,不存在远程攻击者);客户端入口 **reject 不 abort**(不信任对端输入),伪造包只丢不判死
- **ARQ 核心不动**:`ikcp.c` 的可靠传输逻辑保持上游形态,信封在它之外;新增的只有 `RST`/`KICK` 两条控制 cmd 与超时判死(均以 `[adam]` 标注,便于跟上游对比)
- **全小端 + monotonic `uint64` ms**:两端零字节序转换、免 wrap / 免 cast

---

## 许可

[MIT](LICENSE) © 2026
