# `kcp::Server` — 网关 worker

> 网关侧 UDP/KCP 终结点。[typhon::Server](typhon_server.md) 起 N 个(≈核数-1),每个独占**一条线程 + 一个 `SO_REUSEPORT` UDP socket**;eBPF 按 KCP `conv` 路由,同 `conv` 永远落同一个 worker → **share-nothing,全程无锁**。
> 源码:[include/kcp/server.hpp](../include/kcp/server.hpp) · [src/kcp/server.cpp](../src/kcp/server.cpp)

---

## 1. 线程模型与 epoll

单线程 `run()` 跑一个 `epoll_wait`,盯三类 fd,循环尾统一 `update()` 推 KCP:

```plantuml
@startuml
start
:epoll_wait(epfd, INTERVAL=10ms);
:tnow_ = systime_ms();
repeat :遍历就绪事件;
  if (ev.ptr == &evfd_?) then (是)
    :on_event_handle()\n· drain SPSC evque_\n· 处理 AddServ;
  elseif (ev.ptr == &ufd_?) then (是)
    :on_udp_handle()\n· recvmmsg 收客户端包;
  else (否)
    :on_serv_handle()\n· 后端 Connector(TCP) 读写;
  endif
repeat while (还有就绪事件?)
:update()\n· users_ 超时/ikcp_update\n· sque_ 过期清理 + sendmmsg\n· servs_ 心跳;
stop
@enduml
```

| fd | 触发模式 | 用途 |
|---|---|---|
| `ufd_`(UDP) | **故意非 ET** | recvmmsg 限量收包;有残留下轮再唤醒 → 防攻击者大量发包 DoS 把 worker 钉死 |
| `evfd_`(eventfd) | ET | typhon 主线程经 SPSC `evque_` 投递 `AddServ`,敲此 fd 唤醒 |
| serv fd(后端 TCP) | ET | `Connector` 的连接进度 / 收包 |

---

## 2. 核心数据结构

```plantuml
@startuml
hide empty members

class "kcp::Server" as S {
  - SOCKET ufd_, epfd_, evfd_
  - void*  onwer_      ' → typhon::Server (worker→主线程回流)
  - UserMap users_     ' conv → Session
  - ServMap servs_     ' serv_id → Connector
  - SndBuf::Que sque_  ' 待 sendmmsg 的发送队列
  - SndBufPool sb_pool_
  - SPSC evque_        ' 主线程→本 worker 的事件队列
  - mmsghdr rmsgs_[128] ' recvmmsg 批量收
  --
  run() / update()
  on_udp_handle() / on_serv_handle() / on_event_handle()
  {static} output()   ' KCP 发送回调
}

class "kcp::Session" as Sess {
  - ikcpcb* kcp_
  - bool authed_
  - Xx20Key tx_key_, rx_key_  ' 会话密钥(32B)
  - uint32 snd_seq_, rcv_req_ ' 发序号 / 收幂等
  --
  input() : 喂 ikcp_input
  recv()  : 取包 + 解密 + 幂等
  send()  : 加密 + ikcp_send
}

class "tcp::Connector" as Conn {
  状态机: Disconnected/Connecting/Connected
  到后端的 TCP 长连 + 心跳
}

class "core::SndBuf" as SB {
  sockaddr addr; uint8 buf[UDP_MTU]
  siphash 段(前 8B = envelope MAC)
}

S "1" *-- "N" Sess : users_ (按 conv)
S "1" *-- "N" Conn : servs_ (按 serv_id)
S "1" o-- "*" SB   : sque_ (对象池复用)
S ..> "typhon::Server" : onwer_ (notify_serv_disconnected)
@enduml
```

---

## 3. 收包路径(客户端 → 网关)

XDP 在内核已校验 envelope MAC,userland **不重复校验**,直接偏移 8B 喂 ikcp:

```plantuml
@startuml
participant "UDP socket" as U
participant "on_udp_handle" as H
participant "Session" as S
participant "dispatch" as D

H -> U : recvmmsg(最多 128×8 轮)
note right: 非 ET, 限量防 DoS
loop 每个 msg
  H -> H : 跳过头 8B envelope MAC\n(XDP 已校验)
  H -> S : input() → ikcp_input
  loop ikcp_recv 取完整包
    H -> S : recv(&pk)
    S -> S : 长度/id/seq/dst 校验
    S -> S : 幂等(rcv_req_) 去重
    S -> S : authed 则 ChaCha20 解密 + 验 tag + 剥 tag
    alt pk.id == PING
      S -> D : on_ping → 回 PONG
    else == REGIST_REQ
      S -> D : on_regist_req → 握手派生密钥
    else 业务
      S -> D : on_c2s → 转发后端(选 Connector)
    end
  end
end
@enduml
```

- `on_c2s` **零拷贝转发**:收包时 body 落在 `rbuf + PKX_HDR_LEN`(预留 10B),转发时原地在前面填 `PackageEx` 头(`src_id = conv`)再复用 `Connector::send`,不复制 payload。
- 协议错(解密失败/越界)→ `remove_session`;后端找不到/写失败**不踢**客户端 session。

---

## 4. 发包路径(网关 → 客户端)

KCP 分片后经 `output` 回调落 `SndBuf` 池,加 SipHash envelope MAC,`update()` 末尾 `sendmmsg` 批量发:

```plantuml
@startuml
participant "Session::send" as S
participant "ikcp_send/flush" as K
participant "output 回调" as O
participant "update()" as U
participant "UDP socket" as N

S -> S : 加密 payload → **独立暂存 buf**\n(不原地改入参缓冲)
S -> K : ikcp_send(头+密文+tag)
K -> O : 分片后逐段回调 output(buf,len)
O -> O : SndBuf 池 acquire\n前 8B 写 SipHash(KCP 头 24B)
O -> U : sque_.emplace_back(sb)
... 循环尾 ...
U -> N : sendmmsg(sque_ 批量)
@enduml
```

---

## 5. 鉴权握手(`on_regist_req`)

```plantuml
@startuml
participant Client
participant "网关 on_regist_req" as G
Client -> G : REGIST_REQ (sealedbox(AuthToken))
G -> G : 1. 网关 X25519 私钥 sealedbox 解密 token
G -> G : 2. 验 expire / 验 conv==session.conv
G -> G : 3. Ed25519 验签(登录服公钥)
G -> G : 4. 生成临时 X25519 对 + x25519_kx_server\n   → 派生 rx/tx 会话密钥
G -> G : 5. set_key + set_authed(true)
G -> Client : REGIST_RSP(网关临时公钥, 明文)
note right: 客户端同样 x25519_kx_client\n派生镜像 rx/tx → 双边前向保密
@enduml
```

---

## 6. `update()` 周期(每轮 epoll 尾)

三件事,全量但顺序执行(单线程):

1. **session 超时 + KCP tick**:遍历 `users_`,超时(`check_timeout`)则摘除 + `on_disconnected`,否则 `ikcp_update` 推进重传/ACK/flush。
2. **发送队列出网卡**:`sque_` 前段过期的 `SndBuf` 直接释放(不发),其余 `sendmmsg` 批量发,合并成一次 erase。
3. **后端心跳/判死**:遍历 `servs_`,`Connector::update` 发 PING / 判死;判死摘除 + 经 `onwer_->notify_serv_disconnected` 经 **pipe 回流**通知主线程(详见 [typhon_server.md](typhon_server.md))。

---

## 7. 后端连接(`on_serv_handle`)

`servs_` 里每个 `Connector` 是到一个后端 instance 的 TCP 长连:
- `EPOLLOUT` + `Connecting` → `getsockopt(SO_ERROR)` 确认连上 → `regist()` 发 REGIST_REQ。
- `EPOLLIN` → 读 TCP 流 → `decode` 切 `PackageEx` → `on_pong` / `on_regist_rsp` / **`on_s2c`**(后端回程 → 查 session 回发客户端)。
- `EPOLLERR/HUP` → `remove_serv`(摘除 + pipe 回流通知主线程重连)。
