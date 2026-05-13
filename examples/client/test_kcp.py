#!/usr/bin/env python3
"""
typhon KCP server sustained-load test.
100 concurrent clients, each loops forever sending 4KB random + verifying echo.
Prints per-second stats. Ctrl+C 触发最终汇总并退出。

Usage:
    make                          # 编译 libkcp.so
    ./examples/server/build/server &   # 起服务端
    python3 test_kcp.py
"""

import ctypes
import os
import signal
import socket
import sys
import threading
import time
from ctypes import c_int, c_uint, c_long, c_void_p, CFUNCTYPE


# SERVER_HOST = '44.197.226.175'
SERVER_HOST = '127.0.0.1'
SERVER_PORT = 5555
NUM_CLIENTS = 12
DATA_SIZE   = 1024
TIMEOUT_SEC = 10.0


_lib = ctypes.CDLL(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'libkcp.so'))

OutputFn = CFUNCTYPE(c_int, c_void_p, c_int, c_void_p, c_void_p)

_lib.ikcp_create.argtypes    = [c_uint, c_void_p];        _lib.ikcp_create.restype    = c_void_p
_lib.ikcp_release.argtypes   = [c_void_p];                _lib.ikcp_release.restype   = None
_lib.ikcp_setoutput.argtypes = [c_void_p, OutputFn];      _lib.ikcp_setoutput.restype = None
_lib.ikcp_send.argtypes      = [c_void_p, c_void_p, c_int];  _lib.ikcp_send.restype  = c_int
_lib.ikcp_recv.argtypes      = [c_void_p, c_void_p, c_int];  _lib.ikcp_recv.restype  = c_int
_lib.ikcp_input.argtypes     = [c_void_p, c_void_p, c_long]; _lib.ikcp_input.restype = c_int
_lib.ikcp_update.argtypes    = [c_void_p, c_uint];        _lib.ikcp_update.restype    = None
_lib.ikcp_flush.argtypes     = [c_void_p];                _lib.ikcp_flush.restype     = None
_lib.ikcp_nodelay.argtypes   = [c_void_p, c_int, c_int, c_int, c_int]; _lib.ikcp_nodelay.restype = c_int
_lib.ikcp_wndsize.argtypes   = [c_void_p, c_int, c_int];  _lib.ikcp_wndsize.restype   = c_int
_lib.ikcp_setmtu.argtypes    = [c_void_p, c_int];         _lib.ikcp_setmtu.restype    = c_int


def now_ms():
    return ctypes.c_uint(time.monotonic_ns() // 1_000_000).value


class Stats:
    def __init__(self):
        self.lock = threading.Lock()
        # 累计（贯穿全程）
        self.total_sent    = 0
        self.total_success = 0
        self.total_fail    = 0
        self.all_latencies = []
        self.total_bytes_out = 0    # 上行：UDP 实际发出去的字节数（含 KCP 头/重传）
        self.total_bytes_in  = 0    # 下行：UDP 实际收到的字节数
        # 区间（每秒清零，给 live 显示）
        self.iv_sent   = 0
        self.iv_succ   = 0
        self.iv_fail   = 0
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
    conv        = 1000 + client_id
    server_addr = (SERVER_HOST, SERVER_PORT)
    sock        = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(0.05)

    kcp = _lib.ikcp_create(conv, None)
    _lib.ikcp_wndsize(kcp, 128, 128)
    _lib.ikcp_nodelay(kcp, 1, 10, 3, 1)
    _lib.ikcp_setmtu(kcp, 1232)

    def output(buf, length, _kcp, _user):
        sock.sendto(ctypes.string_at(buf, length), server_addr)
        stats.record_bytes_out(length)
        return length
    cb = OutputFn(output)
    _lib.ikcp_setoutput(kcp, cb)

    rbuf = ctypes.create_string_buffer(8192)

    try:
        while not stop_event.is_set():
            sent_data = os.urandom(DATA_SIZE)
            received  = bytearray()

            t_start = time.monotonic()
            stats.record_send()

            sbuf = ctypes.create_string_buffer(sent_data, DATA_SIZE)
            _lib.ikcp_send(kcp, sbuf, DATA_SIZE)
            _lib.ikcp_flush(kcp)

            deadline = time.monotonic() + TIMEOUT_SEC
            while time.monotonic() < deadline and len(received) < DATA_SIZE:
                if stop_event.is_set():
                    return
                try:
                    data, _ = sock.recvfrom(2048)
                    stats.record_bytes_in(len(data))
                    ibuf = ctypes.create_string_buffer(data, len(data))
                    _lib.ikcp_input(kcp, ibuf, len(data))
                except socket.timeout:
                    pass

                while True:
                    n = _lib.ikcp_recv(kcp, rbuf, 8192)
                    if n <= 0:
                        break
                    received += rbuf.raw[:n]

                _lib.ikcp_update(kcp, now_ms())

            if len(received) != DATA_SIZE or bytes(received) != sent_data:
                stats.record_fail()
                continue

            stats.record_success((time.monotonic() - t_start) * 1000.0)
    finally:
        _lib.ikcp_release(kcp)
        sock.close()


def fmt_bytes(n):
    # n: bytes 数。自动选 B / KB / MB / GB
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

    print(f'持续压测：{NUM_CLIENTS} 个客户端 × {DATA_SIZE} B -> '
          f'{SERVER_HOST}:{SERVER_PORT}   （按 Ctrl+C 停止）')

    threads = [threading.Thread(target=run_client, args=(i, stats, stop_event))
               for i in range(NUM_CLIENTS)]

    t0 = time.monotonic()
    for t in threads: t.start()

    # 主线程每秒打印一次区间统计，直到 Ctrl+C
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

    # SIGINT 之后等所有 client 线程退出，再出最终汇总
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
