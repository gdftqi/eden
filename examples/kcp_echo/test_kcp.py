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
import struct
import sys
import threading
import time
from ctypes import c_int, c_uint, c_long, c_void_p, CFUNCTYPE


# SERVER_HOST   = '13.250.22.130'
SERVER_HOST = '127.0.0.1'
SERVER_PORT   = 5555
NUM_CLIENTS   = 12        # localhost 没带宽限制，1k 起步；想测上限可继续加
DATA_SIZE     = 800      # 典型 MMO 移动/事件包 100-300B，取 200B
SEND_RATE_HZ  = 20       # 典型 MMO 同步 10-20 Hz，取 15 Hz
TIMEOUT_SEC   = 15.0     # 单条请求超时阈值（超过算 fail）

# ----- Package 协议格式（必须和 typhon C++ 端 package.hpp 保持一致）-----
# struct Package {
#     uint16_t pk_id;        // 业务消息号
#     uint32_t pk_idem;      // 幂等 ID，客户端单调递增，必须 ≠ 0
#     uint32_t pk_dst_id;    // 目标服务类型（路由键），必须 > 0
#     uint8_t  pk_payload[]; // payload
# };
# 所有多字节字段一律网络字节序（big-endian）。
# KCP 方向 Package 不带长度字段，长度由 KCP 消息边界给定（ikcp_recv 返回值）。
HEADER_FMT  = '!HII'                        # big-endian: u16, u32, u32
HEADER_SIZE = struct.calcsize(HEADER_FMT)   # 10 字节
assert HEADER_SIZE == 10

PK_ID_PING  = 1     # 测试用消息号
PK_DST_ID   = 1     # 测试用目标服务（占位）；必须 > 0，否则被 recv_pk 判定 -7 非法


# ===== AES-128-CTR payload 加密 (半加密: header 明文, 只加密 pk_payload) =====
# 用 ctypes 调系统 libcrypto 的 EVP_aes_128_ctr, 与 C++ 端 AES-NI 实现位等价.
# AES key 与 SipHash envelope key 是同一个 (服务端 kcp::Conf::shkey_ 同时用于两者).
_AES_KEY = struct.pack('<QQ', 0x0102030405060708, 0x090A0B0C0D0E0FAA)   # == SH_KEY
assert len(_AES_KEY) == 16

# IV 方向标记, 与 C++ 端 (src/kcp/session.cpp) 完全一致.
DIR_C2S = 0     # client → server (上行): 客户端 send 加密用
DIR_S2C = 1     # server → client (下行): 客户端 recv 解密用

_libcrypto = ctypes.CDLL("libcrypto.so.3")
_libcrypto.EVP_CIPHER_CTX_new.restype   = ctypes.c_void_p
_libcrypto.EVP_CIPHER_CTX_new.argtypes  = []
_libcrypto.EVP_CIPHER_CTX_free.argtypes = [ctypes.c_void_p]
_libcrypto.EVP_aes_128_ctr.restype      = ctypes.c_void_p
_libcrypto.EVP_aes_128_ctr.argtypes     = []
_libcrypto.EVP_EncryptInit_ex.restype   = ctypes.c_int
_libcrypto.EVP_EncryptInit_ex.argtypes  = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p,
                                           ctypes.c_char_p, ctypes.c_char_p]
_libcrypto.EVP_EncryptUpdate.restype    = ctypes.c_int
_libcrypto.EVP_EncryptUpdate.argtypes   = [ctypes.c_void_p, ctypes.c_char_p,
                                           ctypes.POINTER(ctypes.c_int), ctypes.c_char_p, ctypes.c_int]


def make_iv(conv, idem, direction):
    """构造 16B AES-CTR IV, 与 C++ 端 make_iv 完全一致:
       [conv 4B LE][idem 4B LE][dir 1B][7B 0]
       conv/idem 用 little-endian (x86 host 序, 与 C++ memcpy 一致)。"""
    iv = bytearray(16)
    iv[0:4] = struct.pack('<I', conv & 0xFFFFFFFF)
    iv[4:8] = struct.pack('<I', idem & 0xFFFFFFFF)
    iv[8]   = direction
    return bytes(iv)


def aes128_ctr(key, iv, data):
    """AES-128-CTR. CTR 模式 encrypt == decrypt, 一个函数双用; data 任意长度, 输出等长。"""
    if not data:
        return b''
    ctx = _libcrypto.EVP_CIPHER_CTX_new()
    try:
        if _libcrypto.EVP_EncryptInit_ex(ctx, _libcrypto.EVP_aes_128_ctr(), None, key, iv) != 1:
            raise RuntimeError("EVP_EncryptInit_ex failed")
        out = ctypes.create_string_buffer(len(data) + 16)
        outlen = ctypes.c_int(0)
        if _libcrypto.EVP_EncryptUpdate(ctx, out, ctypes.byref(outlen), data, len(data)) != 1:
            raise RuntimeError("EVP_EncryptUpdate failed")
        return out.raw[:outlen.value]   # CTR 流模式, Final 无额外输出, 省略
    finally:
        _libcrypto.EVP_CIPHER_CTX_free(ctx)


def pack_pk(conv, pk_id, pk_idem, pk_dst_id, payload):
    """半加密: header 明文 + 只加密 payload (上行 DIR_C2S)。"""
    enc = aes128_ctr(_AES_KEY, make_iv(conv, pk_idem, DIR_C2S), payload)
    return struct.pack(HEADER_FMT, pk_id, pk_idem, pk_dst_id) + enc


def unpack_pk(conv, data):
    """返回 (pk_id, pk_idem, pk_dst_id, payload) 或 None.
       header 明文读 pk_idem 后, 用它构造 IV 解密 payload (下行 DIR_S2C)。"""
    if len(data) < HEADER_SIZE:
        return None
    pk_id, pk_idem, pk_dst_id = struct.unpack(HEADER_FMT, data[:HEADER_SIZE])
    payload = aes128_ctr(_AES_KEY, make_iv(conv, pk_idem, DIR_S2C), data[HEADER_SIZE:])
    return (pk_id, pk_idem, pk_dst_id, payload)


# ===== Envelope MAC (SipHash-2-4) =====
# typhon 在 UDP wire 上套了一层 envelope MAC:
#   wire = [SipHash tag 8B][KCP frame]
# 客户端发包时必须按同样的 key 算 SipHash 并 prepend,否则被 server 端 XDP DROP.
# key 必须与 server 端 kcp::Conf::shkey_ 完全一致。
#
# C++ 端 (kcp/config.hpp): uint64_t a[2] = { 0x0102030405060708, 0x090A0B0C0D0E0FAA };
#                          memcpy(shkey_, a, 16)
# LE 平台上等价于 16 字节: 08 07 06 05 04 03 02 01 AA 0F 0E 0D 0C 0B 0A 09
SH_KEY = struct.pack('<QQ', 0x0102030405060708, 0x090A0B0C0D0E0FAA)
assert len(SH_KEY) == 16

ENVELOPE_MAC_LEN = 8
# MAC 只覆盖 KCP frame 前 24 字节 (KCP wire header),与 C++ 端 ENVELOPE_MAC_HASH_LEN 一致.
# 设计目标是 DoS 防御:攻击者必须猜对 conv/sn 才能算出合法 MAC.
ENVELOPE_MAC_HASH_LEN = 24


def _siphash24(data: bytes, key: bytes) -> int:
    """SipHash-2-4 (Aumasson & Bernstein 2012), 与 utils::siphash24 / envelope.bpf.c 位等价.
    返回 host-order 64-bit int."""
    MASK64 = (1 << 64) - 1
    def rotl(x, b):
        return ((x << b) | (x >> (64 - b))) & MASK64

    k0 = int.from_bytes(key[:8], 'little')
    k1 = int.from_bytes(key[8:], 'little')

    v0 = (k0 ^ 0x736f6d6570736575) & MASK64    # "somepseu"
    v1 = (k1 ^ 0x646f72616e646f6d) & MASK64    # "dorandom"
    v2 = (k0 ^ 0x6c7967656e657261) & MASK64    # "lygenera"
    v3 = (k1 ^ 0x7465646279746573) & MASK64    # "tedbytes"

    def sipround():
        nonlocal v0, v1, v2, v3
        v0 = (v0 + v1) & MASK64; v1 = rotl(v1, 13); v1 ^= v0; v0 = rotl(v0, 32)
        v2 = (v2 + v3) & MASK64; v3 = rotl(v3, 16); v3 ^= v2
        v0 = (v0 + v3) & MASK64; v3 = rotl(v3, 21); v3 ^= v0
        v2 = (v2 + v1) & MASK64; v1 = rotl(v1, 17); v1 ^= v2; v2 = rotl(v2, 32)

    n = len(data)
    nblocks = n // 8
    for i in range(nblocks):
        m = int.from_bytes(data[i*8 : i*8 + 8], 'little')
        v3 ^= m
        sipround()
        sipround()
        v0 ^= m

    b = (n & 0xFF) << 56
    tail = data[nblocks * 8:]
    for i, byte in enumerate(tail):
        b |= byte << (i * 8)

    v3 ^= b
    sipround()
    sipround()
    v0 ^= b

    v2 ^= 0xFF
    sipround()
    sipround()
    sipround()
    sipround()

    return (v0 ^ v1 ^ v2 ^ v3) & MASK64


_lib_name = 'libkcp.dll' if sys.platform == 'win32' else 'libkcp.so'
_lib = ctypes.CDLL(os.path.join(os.path.dirname(os.path.abspath(__file__)), _lib_name))

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
    """
    模拟 MMO 客户端：固定频率 SEND_RATE_HZ 持续发包，**不等响应**。
    收到的 echo 用 pk_idem 与 pending 表配对，算延迟。
    超过 TIMEOUT_SEC 仍未收到响应的包按 fail 计。
    """
    conv        = 2000 + client_id
    server_addr = (SERVER_HOST, SERVER_PORT)
    sock        = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(0.002)              # 2ms 非阻塞读，让循环及时跑 send/update

    kcp = _lib.ikcp_create(conv, None)
    _lib.ikcp_wndsize(kcp, 128, 128)
    _lib.ikcp_nodelay(kcp, 1, 10, 3, 1)
    # KCP mtu = UDP_MTU(1232) - ENVELOPE_MAC_LEN(8) = 1224
    # 这样 KCP frame 出 output 时最大 1224, prepend 8B MAC 后 wire 总长 1232,
    # 正好 IPv6 minimum MTU 兼容, 不会被 IP 分片.
    _lib.ikcp_setmtu(kcp, 1224)

    def output(buf, length, _kcp, _user):
        frame = ctypes.string_at(buf, length)
        # MAC 只算前 24 字节 (KCP wire header), 与 server 端约定一致
        hash_len = min(length, ENVELOPE_MAC_HASH_LEN)
        mac = _siphash24(frame[:hash_len], SH_KEY).to_bytes(ENVELOPE_MAC_LEN, 'little')
        packet = mac + frame
        sock.sendto(packet, server_addr)
        stats.record_bytes_out(len(packet))
        return length
    cb = OutputFn(output)
    _lib.ikcp_setoutput(kcp, cb)

    rbuf = ctypes.create_string_buffer(65536)
    next_idem = 1                       # 客户端单调递增，必须 ≠ 0
    pending   = {}                      # idem -> (payload, t_start)

    send_interval = 1.0 / SEND_RATE_HZ  # 50ms @ 20Hz
    next_send_at  = time.monotonic()

    try:
        while not stop_event.is_set():
            now = time.monotonic()

            # 1. 到时间就发一包
            if now >= next_send_at:
                payload = os.urandom(DATA_SIZE)
                idem    = next_idem
                next_idem += 1

                pkg_bytes = pack_pk(conv, PK_ID_PING, idem, PK_DST_ID, payload)
                sbuf = ctypes.create_string_buffer(pkg_bytes, len(pkg_bytes))
                _lib.ikcp_send(kcp, sbuf, len(pkg_bytes))
                _lib.ikcp_flush(kcp)

                pending[idem] = (payload, now)
                stats.record_send()

                next_send_at += send_interval
                # 落后太多就把节奏拉回当前时刻，避免突发 burst
                if next_send_at < now:
                    next_send_at = now + send_interval

            # 2. 尝试收 UDP（非阻塞 2ms）
            try:
                data, _ = sock.recvfrom(2048)
                stats.record_bytes_in(len(data))
                # 跳过前 8 字节 envelope MAC (server 发回来的包也带 MAC).
                # 严格起见可以本地再算一次 SipHash 验证, 但 server 不会发坏 MAC,
                # 这里简化只 strip.
                if len(data) >= ENVELOPE_MAC_LEN:
                    frame = data[ENVELOPE_MAC_LEN:]
                    ibuf = ctypes.create_string_buffer(frame, len(frame))
                    _lib.ikcp_input(kcp, ibuf, len(frame))
            except socket.timeout:
                pass

            # 3. drain KCP 收完成的消息，按 idem 配对 pending
            while True:
                n = _lib.ikcp_recv(kcp, rbuf, 65536)
                if n <= 0:
                    break
                parsed = unpack_pk(conv, bytes(rbuf.raw[:n]))
                if parsed is None:
                    continue
                rcv_id, rcv_idem, _, rcv_payload = parsed
                entry = pending.pop(rcv_idem, None)
                if entry is None:
                    continue        # 来历不明（重复响应 / 已超时清除）
                p_payload, p_t_start = entry
                if rcv_id == PK_ID_PING and rcv_payload == p_payload:
                    stats.record_success((time.monotonic() - p_t_start) * 1000.0)
                else:
                    stats.record_fail()

            # 4. 清理超时的 pending（按 fail 算）
            if pending:
                cutoff = now - TIMEOUT_SEC
                expired = [k for k, (_, t) in pending.items() if t < cutoff]
                for k in expired:
                    pending.pop(k, None)
                    stats.record_fail()

            # 5. 推 KCP 状态机
            _lib.ikcp_update(kcp, now_ms())
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

    print(f'MMO 模拟压测：{NUM_CLIENTS} 个客户端 × {SEND_RATE_HZ} Hz × {DATA_SIZE} B payload '
          f'(+ {HEADER_SIZE} B header) -> {SERVER_HOST}:{SERVER_PORT}   （按 Ctrl+C 停止）')

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
