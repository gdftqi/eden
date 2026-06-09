# typhon 待修问题清单

代码审查后尚未修复的问题。优先级从上到下递减。

---

## P1 — 待 Phase 2 定型

### 1. KCP 无 conv 鉴权 → DDoS 矢量

**位置**: [src/kcp/server.cpp](src/kcp/server.cpp)（`on_udp_handle` 里 `get_session` 走 nullptr 分支直接 `Session::create`）

- 任意陌生 conv 直接 `Session::create` 加进 sessions_。
- 30 s timeout 才清，攻击者可在 30 s 内灌爆 sessions_ map。
- 必须加 conv 频率限制 / pre-auth。做自定义消息时一起处理。

---

### 6. 转发路径的内嵌字节序 —— `Pke` 递归翻 vs KCP 透传来的 net 序

**位置**: [include/core/package.hpp](include/core/package.hpp)（`Pke<O>` 的 `hton`/`ntoh` 递归翻外层+内嵌）

字节序已全栈 phantom 化(见 memory `project-typhon-pke-send-byteorder`)：`Pke` 的 `hton`/`ntoh` **递归翻外层+内嵌**，`Pke<Host>` 的语义是"外层+内嵌**整体** host 序"。对「**本地构造**」的包正确：
- 心跳（`Connector::update` 构造 `Pke<Host>`）✅
- 回程（`Session::send` 后端 host 序构造）✅

但「**转发**」(`on_data` 网关→后端)的内嵌来源不同，分两条实现路线：
- **路线 A 解密转发**：用 `recv_pk` 解出的 host 序 Package 转发 → 内嵌 host、外层 host，能直接套 `Pke<Host>`。代价：网关解密了 payload（多花 CPU + 端到端加密在网关终止）。
- **路线 B 零拷贝透传**：直接拿 KCP 收到的原始 wire（**net 序 + 加密 payload**）prepend PackageEx 头转发。这时内嵌是 net 序、外层是 host 序 —— **"外 host / 内 net"混合态**，**不能直接塞 `Pke<Host>` 然后 `hton`**（会把已经是 net 的内嵌又翻回去）。转发又是最高频路径。

现在没爆，是因为 `on_data` 转发还没实现。**接转发时定 A/B**：
- 选 B（推荐，网关只路由不解密、payload 透传更省更安全）→ 给转发单开一条"只翻外层"的发送路径，只 `htons/htonl` 那 3 个 PackageEx 字段，内嵌一字节不碰，绕过 `Pke` 的递归 `hton`。

---

## 安全（2026-06-09 安全评估）

> 骨架(XDP envelope DoS 过滤 + 半加密 + 即将的鉴权)和选型(SipHash / AES-NI / Ed25519)都对，
> 但下面几条是系统性缺口。S1 / S4 / S5 + ISSUE #1 多数能被**"客户端鉴权 + 每会话密钥协商
> (X25519 ECDH + HKDF)"**一步统一解决——做鉴权握手时一并处理；S2 / S3 靠换 AEAD(GCM)解决。

### S1. 静态全局硬编码 `shkey` —— 地基级弱点

**位置**: [include/kcp/config.hpp:141](include/kcp/config.hpp#L141)（`shkey_` 源码硬编码 `0x0102...`）

`envelope MAC` 和 `AES` 都用这**一个全局 key**，而**客户端二进制里就带着它**：
- 逆向任一客户端 = 拿到 key → 能伪造 envelope MAC(绕过 XDP DoS 过滤) + 解密所有人流量 + 伪造任意流量；
- 不能区分客户端(MAC 只证"持 key"，不证身份)；
- 一处泄露 = 全网沦陷，且静态不可轮换。

上层所有 MAC / 加密的安全性都建立在"这个 key 不泄露"上，而客户端持有它 = 迟早泄露。**正解**：鉴权后用 **X25519 ECDH 协商每会话临时 key** 替代全局 `shkey`(每 session 独立 + 前向保密)。

### S2. payload 无完整性(AES-CTR 可被定向篡改)

**位置**: [src/utils/cryptor.cpp](src/utils/cryptor.cpp)（半加密用 AES-128-CTR）

CTR 只加密不认证：攻击者翻转密文位 = 翻转明文对应位(可塑性)，不需密钥即可**定向篡改 payload**；而 envelope MAC **只覆盖 KCP header 24B、不覆盖 payload**，KCP 本身也不校验 payload → 公网客户端段的 payload 可被改而不被发现。**正解**：换 **AES-128-GCM**(或 ChaCha20-Poly1305)，加密 + 认证一体；明文 header 塞 **AAD** 一并认证(解决 S3)。每包 +16B tag(可截断 8B)。

### S3. Package header 无完整性

**位置**: [include/core/package.hpp](include/core/package.hpp)（Package header 半加密下明文）

envelope MAC 只覆盖 **KCP header**，**不覆盖内层 Package header**；半加密下 `pk_id`/`pk_idem`/`pk_dst_id` 是明文。所以**路由键 `pk_dst_id`、幂等 `pk_idem` 既不加密也不被任何 MAC 罩住** → 可被改路由 / 破坏幂等。**正解**：S2 换 GCM 后，把 Package header 作为 **AAD** 认证(明文但防改)。

### S4. 客户端身份认证缺失

当前无客户端鉴权，**任何持 `shkey` 者都能连任意 conv**。**正解**：Ed25519 签名 token 鉴权(开发中)——LOGIN 私钥签发、网关公钥离线验签。

### S5. SipHash + AES 共用一个 `shkey`(key reuse)

同一个 key 既做 SipHash MAC 又做 AES 加密 —— 同 key 多用途是密码学忌讳。**正解**：用 **HKDF** 从 master / session secret 派生出独立的 `mac_key` / `enc_key`(有了 S1 的会话密钥后天然顺带做)。

---

## Bug / 容量（2026-06-08 全量审查）

### B1. `tcp::Server::sessions_[fd]` 用 fd 直接索引 → 实际容量远小于 `MAX_CONN`，超限直接 abort

**位置**: [include/tcp/server.hpp:218](include/tcp/server.hpp#L218)（`sessions_[MAX_CONN]` + `get/add/remove_session` 的 `ASSERT(fd < MAX_CONN)`）

`sessions_[MAX_CONN]`(2048) 用 **fd 号**当下标，但 fd 是进程全局分配的——`lfd`/`epfd`/`stop_evfd` + 每个 proc 的 `evfd` + 后端 Connector fd 等都占号。连接数接近 2048 时新 `accept` 的 fd 会 **>2048**，命中 `ASSERT` → **abort 整个进程**（硬崩，不是优雅拒绝）。`accept4` 返回的 fd 没有 `< MAX_CONN` 的保证。

- 改法候选：① `fd → Session` 的 `unordered_map`（容量不再受 fd 号约束）；② 保留数组但 `fd >= MAX_CONN` 时优雅 `close(fd)` 拒绝 + 把 `MAX_CONN` 调到远高于预期连接数。

### B2. `epfd_` 的 `epoll_ctl` 被主线程和 worker 线程分裂管理

**位置**: [src/tcp/server.cpp](src/tcp/server.cpp) `on_listen_handle`(主线程 ADD) vs [server.hpp:143](include/tcp/server.hpp#L143) `add_session` 里 on_connected 拒绝时的 DEL(worker)

主线程 `accept` 后 ADD，worker 在 `add_session`(on_connected 非 0)里 DEL 同一 fd。`epoll_ctl` 内核侧线程安全、当前时序也对(ADD 同步先于 notify)，但"epfd 增删"被切成两半跨线程，很脆弱，以后加重连/复用极易踩。建议把 epoll 增删收敛到单一线程。

### B3. `udp_bind`/`tcp_connect` 用 `find(':')`，`tcp_listen` 用 `find_last_of(':')`

**位置**: [src/core/typhon.in.cpp:10/142](src/core/typhon.in.cpp#L10) vs :77

三个解析 host 的函数对冒号处理不一致。IPv4 单冒号没事，但传 IPv6 字面量(`[::1]:port`)时 `find(':')` 会切错。当前 IPv6 未支持所以不爆，是埋着的不一致。

---

## 冗余 / 死代码（2026-06-08 全量审查）

### R4. `tcp::Session::user_data_` + `set_user_data`/`get_user_data` 全死

[include/tcp/session.hpp](include/tcp/session.hpp) — 无调用者。后来加的 `id_`/`set_id()` 取代了它，现在是死成员 + 死方法。删。

### R5. `kcp::Server::on_ping` / `on_regist_req` 声明但无定义

[include/kcp/server.hpp:268](include/kcp/server.hpp#L268) / [:272](include/kcp/server.hpp#L272) — 这两个成员函数只在头里声明，[src/kcp/server.cpp](src/kcp/server.cpp) 里**没有定义**，也无任何调用者（没被 ODR-use 所以不报链接错）。客户端→网关方向的 PING / REGIST_REQ 处理实际走的是后端 `tcp::Proc::on_ping` / `on_regist`（另一个类）。网关侧这两个声明是规划残留，删。

---

## 可以做的优化（不是 bug）

### 3. `RcvBuf::buf` 是堆指针，`SndBuf::buf` 是 inline 数组 —— 同名不同物

跨结构体看代码容易愣一下。命名分开（比如 SndBuf 用 `data[UDP_MTU]`）心智负担更轻。低优先级，目前不动。

### 4. `tcp::Session::sbuf_` 没 reserve

**位置**: [include/tcp/session.hpp](include/tcp/session.hpp)

`std::vector<uint8_t> sbuf_ {}` 默认 0 容量，第一次 insert 触发 alloc，grow 时 realloc + memcpy。等「sbuf_ 是热路径」实测确认后再考虑 reserve。

### 5. CPU affinity

PLAN 已挂着。网关 worker 绑核能稳定 tail latency，应当尽早做。

### O4. `on_udp_handle` / `on_serv_handle` 各持一块 thread_local 大 buf

[src/kcp/server.cpp](src/kcp/server.cpp) — `rbuf[PKG_MAX_LEN]`(64KB) + `rbuf[TCP_RBUF_SIZE]`(8KB) 每 worker 线程各一份，worker 多时累加。都是一次性 thread_local，可接受；worker 数很大时可考虑共用一块。低优先级。
