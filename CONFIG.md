# UDP/KCP

## 内核参数
```
net.core.rmem_max     = 16777216     # SO_RCVBUF 天花板, 16MB
net.core.wmem_max     = 16777216     # SO_SNDBUF 天花板
net.core.netdev_max_backlog = 65536  # 网卡→协议栈队列, 软中断处理不及时时的缓冲(默认 1000, 太小)
net.core.somaxconn    = 65535        # 后端 tcp::Server 的 listen backlog(你 listen(SOMAXCONN))
fs.file-max           = 2000000      # 全局 fd 上限(数万连接要够)
```

## SO_RCVBUF / SO_SNDBUF
SO_RCVBUF(UDP) 8MB(高负载 16MB)
SO_SNDBUF(UDP) 4MB
别盲目调大 —— 每个值 ×N 个 worker socket 都占内存,且过大增加排队延迟。用数据驱动:
```
netstat -su | grep -iE "receive buffer errors|packet receive errors"  # UDP 丢包计数, 持续涨就加 rcvbuf
ss -uanp | grep <端口>     # 看 Recv-Q, 长期堆积说明 worker 处理跟不上(这时加 buf 治标, 该优化处理)

```

## 进阶
* CPU pinning:worker N 绑核 N(pthread_setaffinity_np),配合你 eBPF 把同 conv 路由到固定 worker → 整条路径 L1/L2 命中(这条 PLAN.md 里已列为待办)。

* RSS/RPS + IRQ 亲和:网卡多队列硬件分流(RSS)/软件分流(RPS)到多核,中断绑核,避免单核收包瓶颈。

* 网卡 ring buffer:ethtool -G eth0 rx 4096 tx 4096 增大,配合 netdev_max_backlog 防突发丢包。

# TCP

## TCP autotuning

建议把后端 TCP 的 SO_*BUF setsockopt 去掉,靠 autotuning 自己按吞吐伸缩(几乎总比手动固定值好);
只需把内核 autotuning 的上限调够:

```
net.ipv4.tcp_rmem = 4096 131072 16777216   # min default max(max 给到 16MB)
net.ipv4.tcp_wmem = 4096  65536 16777216
net.ipv4.tcp_mem  = 786432 1048576 1572864 # 全局 TCP 内存(页), 高吞吐时放宽
```

## 长连专属:关掉 idle 后慢启动
```
net.ipv4.tcp_slow_start_after_idle = 0
net.core.somaxconn = 65535
```

## 拥塞控制

```
net.ipv4.tcp_congestion_control = bbr   # 高吞吐低延迟; 需内核 ≥4.9 + 加载 tcp_bbr 模块
```

# 安装BBR

```
sudo modprobe tcp_bbr # 打开
sysctl net.ipv4.tcp_available_congestion_control # 验证
```

## 开机自动加载
echo tcp_bbr | sudo tee /etc/modules-load.d/bbr.conf