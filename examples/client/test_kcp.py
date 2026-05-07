#!/usr/bin/env python3
"""
typhon KCP server load test: 100 concurrent clients × 4KB random data each.

Usage:
    make                          # 编译 libkcp.so
    ./examples/server/build/server &   # 起服务端
    python3 test_kcp.py
"""

import ctypes
import os
import socket
import sys
import threading
import time
from ctypes import c_int, c_uint, c_long, c_void_p, CFUNCTYPE


SERVER_HOST = '127.0.0.1'
SERVER_PORT = 5555
NUM_CLIENTS = 100
DATA_SIZE   = 4096
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


def run_client(client_id, results):
    conv        = 1000 + client_id
    server_addr = (SERVER_HOST, SERVER_PORT)
    sock        = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(0.05)

    kcp = _lib.ikcp_create(conv, None)
    _lib.ikcp_wndsize(kcp, 128, 128)
    _lib.ikcp_nodelay(kcp, 1, 10, 3, 1)
    _lib.ikcp_setmtu(kcp, 1232)

    # callback 必须保留引用，否则被 GC 回收会段错误
    def output(buf, length, _kcp, _user):
        sock.sendto(ctypes.string_at(buf, length), server_addr)
        return length
    cb = OutputFn(output)
    _lib.ikcp_setoutput(kcp, cb)

    sent_data = os.urandom(DATA_SIZE)
    received  = bytearray()

    sbuf = ctypes.create_string_buffer(sent_data, DATA_SIZE)
    _lib.ikcp_send(kcp, sbuf, DATA_SIZE)
    _lib.ikcp_flush(kcp)

    rbuf     = ctypes.create_string_buffer(8192)
    deadline = time.monotonic() + TIMEOUT_SEC

    while time.monotonic() < deadline and len(received) < DATA_SIZE:
        try:
            data, _ = sock.recvfrom(2048)
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

    _lib.ikcp_release(kcp)
    sock.close()

    if len(received) != DATA_SIZE:
        results[client_id] = ('TIMEOUT', f'recv {len(received)}/{DATA_SIZE}')
    elif bytes(received) != sent_data:
        # 找第一个不一致的字节位置
        for i in range(DATA_SIZE):
            if received[i] != sent_data[i]:
                results[client_id] = ('MISMATCH', f'first diff at byte {i}')
                return
    else:
        results[client_id] = ('PASS', None)


def main():
    print(f'launching {NUM_CLIENTS} KCP clients × {DATA_SIZE} bytes -> {SERVER_HOST}:{SERVER_PORT}')
    results = [None] * NUM_CLIENTS
    threads = [threading.Thread(target=run_client, args=(i, results)) for i in range(NUM_CLIENTS)]

    t0 = time.monotonic()
    for t in threads: t.start()
    for t in threads: t.join()
    elapsed = time.monotonic() - t0

    pass_n = sum(1 for r in results if r and r[0] == 'PASS')
    fail_n = NUM_CLIENTS - pass_n

    print(f'elapsed {elapsed:.2f}s | PASS {pass_n}/{NUM_CLIENTS} | FAIL {fail_n}')
    if fail_n:
        for i, r in enumerate(results):
            if r and r[0] != 'PASS':
                print(f'  client {i:3d}: {r[0]:8s} {r[1]}')
        sys.exit(1)


if __name__ == '__main__':
    main()
