# 协议层 — `core::Package`

> 客户端 ↔ 网关 ↔ 后端 三段链路的应用层消息格式。所有多字节字段一律 **little-endian(小端)** —— x86 / ARM 原生,两端零字节序转换。
> 源码:[include/core/package.hpp](../Adam/include/core/package.hpp) · [src/core/package.cpp](../Adam/src/core/package.cpp)

---

## 1. 内存态:`meta` + `data`

`Package` 是一个结构,但**两段链路各只用它的一部分**:

```cpp
struct Package {
    struct { uint32_t len, conv, src_addr; }        meta;   // 网关/后端用, 不上 KCP 线
    struct { uint32_t pid, src_id, dst_id;
             uint8_t  payload[]; }                  data;   // 客户端也发这一段
};
```

- **`meta` 不在客户端方向上线** —— 网关收到 KCP 消息后**本地合成**:`conv` = 会话 conv,`src_addr` = 对端地址。客户端伪造不了这两个值,这是后端能信任 `conv`/来源的根据。
- `data.payload` 是**柔性数组**,`Package` 本身不含 payload 存储 —— 谁调用谁供 buffer,见第 4 节。
- 内存里字段都是 `uint32_t`,但**线上 `len` 和 `pid` 只占 2B**。别拿 `sizeof(Package)` 当帧长算,用 `PKG_HDR_LEN`。

---

## 2. 两个方向、两种 wire frame

| 段 | 传输 | 上线的部分 | 长度怎么定 |
|---|---|---|---|
| 客户端 ↔ 网关 | KCP/UDP | 只有 `data` | **无长度字段** —— KCP 消息边界即帧边界 |
| 网关 ↔ 后端 | TCP | `meta` + `data` 整帧 | 帧首 2B `len`,后端 peek 2B 切包 |

```
KCP 方向 (data-only, 客户端 ↔ 网关)
┌────────┬───────────┬───────────┬───────────────┐
│ pid(2) │ src_id(4) │ dst_id(4) │ payload ...    │
└────────┴───────────┴───────────┴───────────────┘
 │<────── PKG_DATA_LEN = 10 ──────>│
   · 这一层不加密 —— 加密在下面的信封层, 包住整个 KCP 数据报(第 5 节)
   · meta 由网关本地合成, 不上线

TCP 方向 (整帧, 网关 ↔ 后端)
┌────────┬─────────┬─────────────┬────────┬───────────┬───────────┬──────────┐
│ len(2) │ conv(4) │ src_addr(4) │ pid(2) │ src_id(4) │ dst_id(4) │ payload  │
└────────┴─────────┴─────────────┴────────┴───────────┴───────────┴──────────┘
 │<──── PKG_META_LEN = 10 ────────>│<────── PKG_DATA_LEN = 10 ─────>│
 │<─────────────── PKG_HDR_LEN = 20 ───────────────────────────────>│
   · len = 整帧总长(含头), 后端 peek 2B 即可分帧
   · conv 是回程路由键; src_addr 让后端拿到来源 IP
   · 明文 —— 内网可信, 加密已在网关卸载
```

---

## 3. codec:四个函数

共享实现在 [`src/core/package.cpp`](../Adam/src/core/package.cpp),客户端 / 网关 / 后端走同一套语义。

| 函数 | 用途 | 返回 |
|---|---|---|
| `data_encode(buf, pk)` | 装 `data` 段(KCP 方向) | 写入字节数 |
| `data_decode(pk, buf, buflen)` | 解 `data` 段 | `0` 成功 / `xERR_PK_LEN` 长度不足 |
| `frame_encode(buf, pk)` | 装整帧(TCP 方向) | 写入字节数 |
| `frame_decode(pk, buf, avail)` | 解整帧,**自带分帧** | 见下 |

> ### ⚠️ `frame_decode` 的返回值有三态,调用方必须判 `< 0`
>
> | 返回 | 含义 |
> |---|---|
> | `> 0` | 成功,值 = **本帧消费的字节数**(调用方据此推进读游标) |
> | `== 0` | **半包** —— 缓冲里还不够一整帧,保留数据等下次读 |
> | `< 0` | 帧长非法(`xERR_PK_LEN`) |
>
> 半包用 `0` 表示,所以 **`if (res != 0)` 是错的** —— 那会把正常的半包当成错误。必须写 `if (res < 0)`。源码里为此专门留了一行注释。

`frame_encode` 的 `data` 段直接复用 `data_encode`,两个方向的 `data` 布局因此永远一致,不会各改各的漂掉。

---

## 4. 零分配收包契约

> **`recv(Package*)` 把包解进调用方提供的 buffer,内部绝不分配。**

- codec 只做 `memcpy`,不 `new`、不 `malloc`。`data_decode` 会把 payload 拷进 `pk->data.payload` —— 所以**调用方的 buffer 必须自己够大**,`Package` 结构体本身不含 payload 存储。
- 每线程一块 `alignas(Package) static thread_local` 复用的 Package:同线程 decode → 消费 → 下一条覆盖,热路径上没有 per-message 的 malloc/free。同一时刻一条线程只有一个包活着,所以**不需要对象池**。
- 只有**跨线程转交**(s2c 回程要转给 owner Worker)才 COPY 一份,走 mimalloc。

**payload 长度允许为 0** —— 包括 authed 之后。`data_decode` 的下界是 `PKG_DATA_LEN`,不是 `PKG_DATA_LEN + 1`;上层判包时别把空 payload 当非法。

---

## 5. 加密不在这一层

历史上 AEAD 曾经坐在 `Package` 上(头明文、只加密 payload、`seq` 当 nonce),**这套已经废弃**。现在安全层**下沉到 KCP 之外**,整个 KCP 数据报被裹在信封里:

```
UDP 载荷
偏移  0            8        12       16                        末尾-16
     ┌─槽位 MAC─┬─ conv ─┬─计数器─┬── AEAD(整个 KCP 数据报) ──┬─ tag ─┐
     │   8B     │   4B   │  4B    │   明文 = 24B KCP头 + 数据  │  16B  │
     └──────────┴────────┴────────┴───────────────────────────┴───────┘
                 └────── AAD(认证但不加密) ──────┘
```

对 `Package` 层的影响只有一条:**它看到的永远是明文,不用关心 tag,长度账里也不含 tag**。开销 32B/包由信封层承担(`ENVELOPE_OVERHEAD`),`KCP_MTU` = `UDP_MTU`(1450) − 32 = **1418**。

细节见 [README 的传输安全层](../README.md#传输安全层)与 [`core/adam.in.hpp`](../Adam/include/core/adam.in.hpp) 里的信封布局注释。

> **应用层 `seq` 已删除。** 它原本兼两个职责:幂等去重与 AEAD nonce。前者是重复造轮子 —— `ikcp_parse_data` 自带 sn 去重,重复段只回 ACK 不入 `rcv_queue`;后者随安全层下沉,改由信封层的计数器承担(它同时也是防重放序号)。

---

## 6. 关键常量

| 常量 | 值 | 含义 |
|---|---|---|
| `PKG_META_LEN` | 10 | `meta` 上线宽度(len 2 + conv 4 + src_addr 4) |
| `PKG_DATA_LEN` | 10 | `data` 上线宽度(pid 2 + src_id 4 + dst_id 4) |
| `PKG_HDR_LEN` | 20 | 整帧头 = `PKG_META_LEN + PKG_DATA_LEN` |
| `PKG_MAX_LEN` | 65535 | 整帧总长上限(`len` 是 uint16) |
| `UDP_MTU` | 1450 | 单个 UDP 包上限 |
| `ENVELOPE_OVERHEAD` | 32 | 信封开销 = 头 16 + AEAD tag 16 |
| `KCP_MTU` | 1418 | `UDP_MTU - ENVELOPE_OVERHEAD`,含 24B KCP 头 |

---

## 7. 消息号(PID)

`100 ~ 199` 是框架保留段,`PID_CUSTOM = 200` 起归业务。

| PID | 值 | 方向 | 含义 |
|---|---|---|---|
| `PID_PING` / `PID_PONG` | 100 / 101 | 双向 | 心跳(兼打延迟) |
| `PID_BKD_REG_REQ` / `RSP` | 102 / 103 | 网关 ↔ 后端 | 后端注册 |
| `PID_TER_REG_REQ` / `RSP` | 102 / 103 | 客户端 → 网关 | 终端注册(**与后端注册同号**,靠链路方向区分) |
| `PID_TER_KIC_REQ` / `RSP` / `NTF` | 104 / 105 / 106 | — | 踢除终端 |
| `PID_TER_ENT_REQ` / `RSP` | 107 / 108 | — | 终端进入后端 |
| `PID_TER_LEA_REQ` / `RSP` | 109 / 110 | — | 终端离开后端 |
| `PID_TER_OFF_NTF` | 111 | → 后端 | 终端下线通知 |
| `PID_TER_BIND_NTF` / `PID_TER_UNBD_NTF` | 112 / 113 | — | 终端绑定 / 解绑变更 |
| `PID_TER_ERROR` | 114 | 网关 → 客户端 | 请求处理失败(目标后端未就绪等,**不断连**) |
| `PID_CUSTOM` | 200 | — | 业务自定义起点 |

> `PID_TER_ENT_REQ` 由客户端发出但**网关拦截不转发** —— 网关用会话状态重新盖章后才发给后端,防止客户端伪造身份进别人的后端。链路细节见 [kcp_server.md](kcp_server.md)。

上线的错误码(`PERR_*` / `SERR_*`)不在本文件,见 [`core/error.hpp`](../Adam/include/core/error.hpp)。
