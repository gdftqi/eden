# `tcp::Server` — 后端业务服骨架

> 后端 instance 的 TCP 服务端。**只接网关连接**(不暴露给客户端,不存在远程攻击者 → 协议层 `ASSERT`-on-malformed 是合理的 fail-fast)。
> 主线程 `accept` + 收发,N 个 `Proc` worker 跑业务派发;同一 `fd` 始终落同一 worker(`fd % ws`)→ **session 不跨线程,无锁**。
> 源码:[include/tcp/server.hpp](../include/tcp/server.hpp) · [src/tcp/server.cpp](../src/tcp/server.cpp) · [src/tcp/proc.cpp](../src/tcp/proc.cpp)

---

## 1. 线程划分

```plantuml
@startuml
hide empty members

class "tcp::Server (主线程)" as S {
  - SOCKET lfd_, stop_evfd_, epfd_
  - Session::Ptr gws_[MAX_CONN]      ' fd → Session
  - PackageHandler handlers[MAX_HANDLERS]
  - vector<Proc::Ptr> procs_
  --
  run(): accept + epoll(所有 session fd)
  on_listen_handle() / on_session_handle()
  regist_handler(pkid, fn)
}

class "tcp::Proc (worker)" as P {
  - int id_
  - SOCKET epfd_, evfd_
  - SPSC<QEvent*> evque_
  --
  run(): 处理本 worker 的 QEvent
  check_timeout(): 按 i=id_; i+=ws 分片扫超时
}

class "tcp::Session" as Sess {
  - uint32 id_   ' 0 = 未鉴权
  - RcvBuf rbuf_ ' 切包(动态扩容线性 buffer)
  - vector sbuf_ ' 发送暂存(partial write)
  + input()/recv()/send()
}

S "1" *-- "N" P : procs_
S "1" *-- "MAX_CONN" Sess : gws_ (按 fd)
P ..> S : server_->get_session / remove_session
note bottom of Sess : 同一 fd 只被\nfd%ws 那个 Proc 读写
@enduml
```

**职责切分:**
- **主线程**:`accept` 新连接 + 把所有 session fd 放进自己的 epoll + `recv` 原始字节,然后**只 push 事件**给 worker,不碰业务。
- **Proc(worker)**:从 SPSC 队列取事件 → 切包 → 查 `handlers[]` 派发业务 → 发送 → 超时清理。`gws_[fd]` 只被对应 worker 读写。

---

## 2. 主线程 ↔ worker:统一事件队列

主线程通过 **SPSC 无锁队列 + eventfd 唤醒** 把四类事件投给 worker:

```plantuml
@startuml
participant "客户端\n(网关)" as C
participant "主线程\non_listen/on_session" as M
participant "Proc.evque_\n(SPSC)" as Q
participant "Proc.run\n(worker)" as W

C -> M : connect
M -> M : accept4 + epoll ADD
M -> Q : notify(AddSess, fd)
Q -> W : AddSess → add_session(fd)\n→ on_connected 回调

C -> M : 数据到
M -> M : recv 原始字节
M -> Q : notify(Recv, RecvArg{fd,data})
Q -> W : Recv → session.input → 切包\n→ handlers[pk_id] 派发

M -> Q : (EPOLLOUT) notify(Send, fd)
Q -> W : Send → session.send 续 flush

C -> M : 断开(EOF/HUP)
M -> M : epoll_ctl DEL (主线程立即删)
M -> Q : notify(RmvSess, fd)
Q -> W : RmvSess → remove_session(清 gws_)
@enduml
```

> `QEvent { Stop, Recv, Send, AddSess, RmvSess }` 抽在 [core/qevent.hpp](../include/core/qevent.hpp)。`Recv` 携带 `RcvArg{fd, len, data[]}`(mi_malloc,worker 处理后 free)。

---

## 3. 收包 → 业务派发

```plantuml
@startuml
start
:主线程 recv(fd) 原始字节;
:notify(Recv, RecvArg);
:worker on_recv_handle;
:session.input() → rbuf_.append;
repeat
  :rbuf_.decode() 切出一个 PackageEx;
  if (pk.id == PING?) then (是)
    :on_ping → 回 PONG;
  elseif (pk.id == REGIST_REQ?) then (是)
    :on_regist → set_id + 回 REGIST_RSP;
  else (业务)
    if (authed? (id_ != 0)) then (是)
      :handlers[pk.id](session, pkx);
    else (否)
      :WARN 未鉴权, 丢弃;
    endif
  endif
repeat while (还有完整包?)
stop
@enduml
```

- `handlers[MAX_HANDLERS]` 是**裸数组,O(1) 派发**,**必须在 `run()` 之前 `regist_handler` 注册完**(运行中 worker 只读,无同步)。
- `RcvBuf`:lazy 分配,双游标 `rpos/wpos` + 阈值 `compact`;`decode` peek 2B `PackageEx.len` 切包,半包返回 `xAGAIN`。

---

## 4. `IEvent` 回调的线程语义(⚠️ 实现方注意)

```plantuml
@startuml
hide empty members
interface IEvent {
  on_init(Server*)          ' 主线程, 1 次
  on_stopped(Server*)       ' 主线程, 1 次
  on_connected(Session)     ' **worker 线程, 多个并发**
  on_disconnected(Session)  ' **worker 线程, 多个并发**
}
note right of IEvent
  on_connected/on_disconnected 由不同 worker 并发触发
  (不同 fd 落不同 worker)。实现方写共享容器(玩家表等)
  要自己加锁 / shard by fd。
  同一 fd 的回调不会重入(同 fd 同 worker)。
end note
@enduml
```

---

## 5. session 生命周期与已知点

- **容器**:`gws_[fd]`(shared_ptr 数组,按 fd 索引,业务可跨调用安全持有,不会 UAF)。
- **超时**:worker `check_timeout` 每秒按 `i = id_; i += ws` 切片扫自己负责的 session,`last_recv_ms` 超时则 `remove_session`。
- **删除路径**:主线程检测断开(EOF/HUP)**立即** `epoll_ctl DEL`(EPOLLHUP 在 ET 下持续报,不立即删会空转)+ notify worker 清 `gws_`。
- **已知点(双重 DEL)**:主线程断开 DEL 与 worker `check_timeout` DEL 可能撞同一 fd → 第二次 `ENOENT`。当前用 **容忍 ENOENT** 处理(`epoll_ctl DEL` 失败若 `errno==ENOENT` 放过);更彻底的 "fd close/epoll 收同一线程" 因 fd 复用 ABA 留作 backlog。
- **release 顺序**:`procs_.clear()` 必须在 `threads_.join()` 之后(否则 worker 持有的 `this` 变 UAF)。
