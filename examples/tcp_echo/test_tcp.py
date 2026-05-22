#!/usr/bin/env python3
"""
typhon TCP echo server sustained-load test.
N concurrent clients, each loops forever sending random + verifying echo.
Prints per-second stats. Ctrl+C 触发最终汇总并退出。

Usage:
    make                          # 编译 server
    ./build/server &              # 起服务端
    python3 test_tcp.py
"""

import os
import signal
import socket
import struct
import sys
import threading
import time


SERVER_HOST   = '127.0.0.1'
SERVER_PORT   = 6688
NUM_CLIENTS   = 5         # localhost 没带宽限制,加大就增加压力
DATA_SIZE     = 800       # 典型 MMO 移动/事件包 100-300B,取 800B
SEND_RATE_HZ  = 20        # 典型 MMO 同步 10-20 Hz,取 20 Hz
TIMEOUT_SEC   = 15.0      # 单条请求超时阈值(超过算 fail)

# ----- Package 协议格式(必须和 typhon C++ 端 package.hpp 保持一致)-----
# struct Package {
#     uint16_t pk_len;     // 整包总长 = HEADER + payload + TAIL (后端方向 pk_len 含 tail)
#     uint16_t pk_id;      // 业务消息号
#     uint32_t pk_idem;    // 幂等 ID,客户端单调递增,必须 ≠ 0
#     uint32_t pk_dst_id;  // 目标服务类型(路由键)
#     uint8_t  pk_data[];  // payload
# };
# struct PackageTail {        // 网关 stamp,**到 backend 的包必然带这 8 字节**
#     uint32_t pkt_src_id;    // FromPlayerID
#     uint32_t pkt_src_addr;  // 客户端 IPv4 地址(uint32)
# };
# 所有多字节字段一律网络字节序(big-endian)。
# 本测试直连 backend,所以自己模拟 gateway 把 tail 也打上。
HEADER_FMT  = '!HHII'
HEADER_SIZE = struct.calcsize(HEADER_FMT)
TAIL_FMT    = '!II'
TAIL_SIZE   = struct.calcsize(TAIL_FMT)
assert HEADER_SIZE == 12
assert TAIL_SIZE   == 8

PK_ID_PING = 1
PK_DST_ID  = 0

# 模拟 gateway stamp 的 tail 字段(测试里固定值即可)
PKT_SRC_ID   = 0
PKT_SRC_ADDR = 0x7f000001   # 127.0.0.1


def pack_pk(pk_id, pk_idem, pk_dst_id, payload):
    pk_len = HEADER_SIZE + len(payload) + TAIL_SIZE
    return (struct.pack(HEADER_FMT, pk_len, pk_id, pk_idem, pk_dst_id)
            + payload
            + struct.pack(TAIL_FMT, PKT_SRC_ID, PKT_SRC_ADDR))


def unpack_pk(data):
    """ 返回 (pk_id, pk_idem, pk_dst_id, payload) 或 None。tail 校验后丢弃 """
    if len(data) < HEADER_SIZE + TAIL_SIZE:
        return None
    pk_len, pk_id, pk_idem, pk_dst_id = struct.unpack(HEADER_FMT, data[:HEADER_SIZE])
    if pk_len != len(data):
        return None
    payload = data[HEADER_SIZE:-TAIL_SIZE]
    return (pk_id, pk_idem, pk_dst_id, payload)


class Stats:
    def __init__(self):
        self.lock = threading.Lock()
        # 累计(贯穿全程)
        self.total_sent      = 0
        self.total_success   = 0
        self.total_fail      = 0
        self.all_latencies   = []
        self.total_bytes_out = 0
        self.total_bytes_in  = 0
        # 区间(每秒清零,给 live 显示)
        self.iv_sent      = 0
        self.iv_succ      = 0
        self.iv_fail      = 0
        self.iv_lat_sum   = 0.0
        self.iv_lat_count = 0
        self.iv_bytes_out = 0
        self.iv_bytes_in  = 0

    def record_send(self):
        with self.lock:
            self.total_sent += 1
            self.iv_sent    += 1

    def record_success(self, latency_ms):
        with self.lock:
            self.total_success += 1
            self.iv_succ       += 1
            self.all_latencies.append(latency_ms)
            self.iv_lat_sum    += latency_ms
            self.iv_lat_count  += 1

    def record_fail(self):
        with self.lock:
            self.total_fail += 1
            self.iv_fail    += 1

    def record_bytes_out(self, n):
        with self.lock:
            self.total_bytes_out += n
            self.iv_bytes_out    += n

    def record_bytes_in(self, n):
        with self.lock:
            self.total_bytes_in += n
            self.iv_bytes_in    += n

    def snapshot_interval(self):
        with self.lock:
            s = (self.iv_sent, self.iv_succ, self.iv_fail,
                 self.iv_lat_sum, self.iv_lat_count,
                 self.iv_bytes_out, self.iv_bytes_in)
            self.iv_sent = self.iv_succ = self.iv_fail = 0
            self.iv_lat_sum = 0.0
            self.iv_lat_count = 0
            self.iv_bytes_out = 0
            self.iv_bytes_in  = 0
            return s


def run_client(client_id, stats, stop_event):
    """
    模拟 MMO 客户端:固定频率 SEND_RATE_HZ 持续发包,**不等响应**。
    收到的 echo 用 pk_idem 与 pending 表配对,算延迟。
    超过 TIMEOUT_SEC 仍未收到响应的包按 fail 计。
    """
    server_addr = (SERVER_HOST, SERVER_PORT)
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        sock.connect(server_addr)
    except Exception as e:
        print(f'[client {client_id}] connect failed: {e}')
        return

    sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    sock.setblocking(False)

    next_idem = 1                       # 客户端单调递增,必须 ≠ 0
    pending   = {}                      # idem -> (payload, t_start)

    send_interval = 1.0 / SEND_RATE_HZ
    next_send_at  = time.monotonic()

    recv_buf = bytearray()

    try:
        while not stop_event.is_set():
            now = time.monotonic()

            # 1. 到时间就发一包
            if now >= next_send_at:
                payload = os.urandom(DATA_SIZE)
                idem    = next_idem
                next_idem += 1

                pkg_bytes = pack_pk(PK_ID_PING, idem, PK_DST_ID, payload)
                try:
                    # TCP 一次 sendall(底层已建立连接)
                    total = len(pkg_bytes)
                    sent = 0
                    while sent < total:
                        try:
                            n = sock.send(pkg_bytes[sent:])
                            if n <= 0:
                                break
                            sent += n
                        except BlockingIOError:
                            # 内核 buffer 满,稍等再试
                            time.sleep(0.001)
                    stats.record_bytes_out(sent)
                except Exception:
                    break

                pending[idem] = (payload, now)
                stats.record_send()

                next_send_at += send_interval
                if next_send_at < now:
                    next_send_at = now + send_interval

            # 2. 非阻塞 recv,尽量读
            try:
                data = sock.recv(8192)
                if data:
                    stats.record_bytes_in(len(data))
                    recv_buf.extend(data)
                elif data == b'':
                    # 对端关闭
                    break
            except BlockingIOError:
                pass
            except Exception:
                break

            # 3. 按 pk_len 切完整 Package,按 idem 配对 pending
            while len(recv_buf) >= HEADER_SIZE:
                pk_len = struct.unpack('!H', bytes(recv_buf[:2]))[0]
                if pk_len > len(recv_buf):
                    break       # 还没收齐
                pkg_bytes = bytes(recv_buf[:pk_len])
                del recv_buf[:pk_len]

                parsed = unpack_pk(pkg_bytes)
                if parsed is None:
                    continue
                rcv_id, rcv_idem, _, rcv_payload = parsed
                entry = pending.pop(rcv_idem, None)
                if entry is None:
                    continue
                p_payload, p_t_start = entry
                if rcv_id == PK_ID_PING and rcv_payload == p_payload:
                    stats.record_success((time.monotonic() - p_t_start) * 1000.0)
                else:
                    stats.record_fail()

            # 4. 清理超时的 pending(按 fail 算)
            if pending:
                cutoff = now - TIMEOUT_SEC
                expired = [k for k, (_, t) in pending.items() if t < cutoff]
                for k in expired:
                    pending.pop(k, None)
                    stats.record_fail()

            # 5. 避免空转
            time.sleep(0.001)
    finally:
        sock.close()


def fmt_bytes(n):
    n = float(n)
    if n < 1024:                 return f'{n:6.0f} B '
    n /= 1024
    if n < 1024:                 return f'{n:6.2f} KB'
    n /= 1024
    if n < 1024:                 return f'{n:6.2f} MB'
    n /= 1024
    return                              f'{n:6.2f} GB'


def percentile(sorted_values, q):
    if not sorted_values:
        return 0.0
    idx = int(len(sorted_values) * q)
    if idx >= len(sorted_values):
        idx = len(sorted_values) - 1
    return sorted_values[idx]


def main():
    stats      = Stats()
    stop_event = threading.Event()

    def handle_sig(sig, frame):
        stop_event.set()
    signal.signal(signal.SIGINT,  handle_sig)
    signal.signal(signal.SIGTERM, handle_sig)

    print(f'TCP MMO 模拟压测:{NUM_CLIENTS} 个客户端 × {SEND_RATE_HZ} Hz × {DATA_SIZE} B payload '
          f'(+ {HEADER_SIZE} B header + {TAIL_SIZE} B tail) -> {SERVER_HOST}:{SERVER_PORT}   '
          f'(按 Ctrl+C 停止)')

    threads = [threading.Thread(target=run_client, args=(i, stats, stop_event))
               for i in range(NUM_CLIENTS)]

    t0 = time.monotonic()
    for t in threads: t.start()

    next_tick = t0 + 1.0
    while not stop_event.is_set():
        time.sleep(0.05)
        now = time.monotonic()
        if now >= next_tick:
            sent, succ, fail, lat_sum, lat_count, b_out, b_in = stats.snapshot_interval()
            avg     = (lat_sum / lat_count) if lat_count else 0.0
            print(f'[+{int(now - t0):4d}s]  发送 {sent:5d}  成功 {succ:5d}  失败 {fail:4d}  '
                  f'平均延迟 {avg:6.2f} ms   ↑{fmt_bytes(b_out)}/s  ↓{fmt_bytes(b_in)}/s')
            next_tick += 1.0

    print('\n停止中 ...')
    for t in threads: t.join()
    elapsed = time.monotonic() - t0

    with stats.lock:
        all_lat    = list(stats.all_latencies)
        total_sent = stats.total_sent
        total_succ = stats.total_success
        total_fail = stats.total_fail
        total_out  = stats.total_bytes_out
        total_in   = stats.total_bytes_in
    all_lat.sort()

    print('=' * 64)
    print(f'运行时间     {elapsed:.2f} 秒')
    print(f'请求统计     发送 {total_sent}   成功 {total_succ}   失败 {total_fail}')
    if total_succ:
        app_bps = total_succ * DATA_SIZE / elapsed
        print(f'吞吐量       {total_succ / elapsed:.0f} 次/s   ({fmt_bytes(app_bps)}/s 应用层)')
    print(f'网络流量     ↑ {fmt_bytes(total_out)}   ↓ {fmt_bytes(total_in)}   '
          f'(↑{fmt_bytes(total_out / elapsed)}/s  ↓{fmt_bytes(total_in / elapsed)}/s)')
    if all_lat:
        avg = sum(all_lat) / len(all_lat)
        print(f'延迟 (ms)    最小 {all_lat[0]:.2f}  平均 {avg:.2f}  '
              f'p50 {percentile(all_lat, 0.50):.2f}  p95 {percentile(all_lat, 0.95):.2f}  '
              f'p99 {percentile(all_lat, 0.99):.2f}  最大 {all_lat[-1]:.2f}')
    print('=' * 64)


if __name__ == '__main__':
    main()
