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

## 可以做的优化（不是 bug）

### 3. `RcvBuf::buf` 是堆指针，`SndBuf::buf` 是 inline 数组 —— 同名不同物

跨结构体看代码容易愣一下。命名分开（比如 SndBuf 用 `data[UDP_MTU]`）心智负担更轻。低优先级，目前不动。

### 4. `tcp::Session::sbuf_` 没 reserve

**位置**: [include/tcp/session.hpp](include/tcp/session.hpp)

`std::vector<uint8_t> sbuf_ {}` 默认 0 容量，第一次 insert 触发 alloc，grow 时 realloc + memcpy。等「sbuf_ 是热路径」实测确认后再考虑 reserve。

### 5. CPU affinity

PLAN 已挂着。网关 worker 绑核能稳定 tail latency，应当尽早做。
