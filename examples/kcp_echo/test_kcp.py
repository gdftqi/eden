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
import base64
import hashlib
from ctypes import c_int, c_uint, c_long, c_void_p, CFUNCTYPE


# SERVER_HOST = '13.214.204.197'
SERVER_HOST = '127.0.0.1'
SERVER_PORT   = 5555
NUM_CLIENTS   = 2        # localhost 没带宽限制，1k 起步；想测上限可继续加
DATA_SIZE     = 800      # 典型 MMO 移动/事件包 100-300B，取 200B
SEND_RATE_HZ  = 20       # 典型 MMO 同步 10-20 Hz，取 15 Hz
TIMEOUT_SEC   = 5.0      # 单条请求超时阈值（超过算 fail）

# ----- Package 协议格式（必须和 typhon C++ 端 package.hpp 保持一致）-----
# struct Package {
#     uint16_t id;        // 业务消息号
#     uint32_t seq;       // 序号，客户端单调递增，必须 ≠ 0 (兼作 AES-CTR IV 输入)
#     uint32_t dst_id;    // 目标服务 (路由键)，必须 > 0
#     uint8_t  payload[]; // payload
# };
# 所有多字节字段一律网络字节序（big-endian）。
# KCP 方向 Package 不带长度字段，长度由 KCP 消息边界给定（ikcp_recv 返回值）。
HEADER_FMT  = '!HIII'                       # big-endian: id u16, src_id u32, dst_id u32, seq u32
HEADER_SIZE = struct.calcsize(HEADER_FMT)   # 14 字节
assert HEADER_SIZE == 14

PK_ID_PING  = 1     # 业务 echo 消息号 (非 100/102, 走 server on_c2s 转发后端)
PK_DST_ID   = 10000 # 目标后端服务 id；必须 > 0
GATEWAY_ID  = 1000  # 网关 id (== config.yml id)，REGIST_REQ 的 dst_id 必须等于它

# 鉴权握手消息号 (与 C++ package.hpp 的 PKID_* 一致)
PKID_REGIST_REQ = 102
PKID_REGIST_RSP = 103


# ===== ChaCha20-Poly1305 AEAD payload 加密 (与 C++ 端 utils::xx20_* 位等价) =====
# 用 ctypes 调系统 libcrypto 的 EVP_chacha20_poly1305 (RFC 8439); 与服务端 libsodium
# crypto_aead_chacha20poly1305_ietf_* 完全兼容 (12B nonce, 16B Poly1305 tag)。
#   - header 明文, 只加密 payload; authed 之前的 REGIST_REQ/RSP 走明文。
#   - 会话密钥 32B, 握手后由 X25519 ECDH 派生 (上行 tx, 下行 rx), 见 kx_client。
#   - wire body = 密文 + 16B tag (tag 附密文尾部, 与服务端 detached 约定一致)。
#   - 无 AAD (与服务端 xx20_* 调用一致)。envelope MAC 的 SipHash key 仍是固定的 SH_KEY。

# 方向标记, 与 C++ 端 (src/kcp/session.cpp) 完全一致.
DIR_C2S = 0     # client → server (上行): 客户端 send 加密用 tx_key
DIR_S2C = 1     # server → client (下行): 客户端 recv 解密用 rx_key

XX20_NONCE_LEN = 12
XX20_TAG_LEN   = 16

# EVP AEAD ctrl 命令字 (openssl/evp.h)
_EVP_CTRL_AEAD_SET_IVLEN = 0x9
_EVP_CTRL_AEAD_GET_TAG   = 0x10
_EVP_CTRL_AEAD_SET_TAG   = 0x11

_libcrypto = ctypes.CDLL("libcrypto.so.3")
_libcrypto.EVP_CIPHER_CTX_new.restype     = ctypes.c_void_p
_libcrypto.EVP_CIPHER_CTX_new.argtypes    = []
_libcrypto.EVP_CIPHER_CTX_free.argtypes   = [ctypes.c_void_p]
_libcrypto.EVP_chacha20_poly1305.restype  = ctypes.c_void_p
_libcrypto.EVP_chacha20_poly1305.argtypes = []
_libcrypto.EVP_CIPHER_CTX_ctrl.restype    = ctypes.c_int
_libcrypto.EVP_CIPHER_CTX_ctrl.argtypes   = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_void_p]
_libcrypto.EVP_EncryptInit_ex.restype     = ctypes.c_int
_libcrypto.EVP_EncryptInit_ex.argtypes    = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p,
                                             ctypes.c_char_p, ctypes.c_char_p]
_libcrypto.EVP_EncryptUpdate.restype      = ctypes.c_int
_libcrypto.EVP_EncryptUpdate.argtypes     = [ctypes.c_void_p, ctypes.c_char_p,
                                             ctypes.POINTER(ctypes.c_int), ctypes.c_char_p, ctypes.c_int]
_libcrypto.EVP_EncryptFinal_ex.restype    = ctypes.c_int
_libcrypto.EVP_EncryptFinal_ex.argtypes   = [ctypes.c_void_p, ctypes.c_char_p, ctypes.POINTER(ctypes.c_int)]
_libcrypto.EVP_DecryptInit_ex.restype     = ctypes.c_int
_libcrypto.EVP_DecryptInit_ex.argtypes    = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p,
                                             ctypes.c_char_p, ctypes.c_char_p]
_libcrypto.EVP_DecryptUpdate.restype      = ctypes.c_int
_libcrypto.EVP_DecryptUpdate.argtypes     = [ctypes.c_void_p, ctypes.c_char_p,
                                             ctypes.POINTER(ctypes.c_int), ctypes.c_char_p, ctypes.c_int]
_libcrypto.EVP_DecryptFinal_ex.restype    = ctypes.c_int
_libcrypto.EVP_DecryptFinal_ex.argtypes   = [ctypes.c_void_p, ctypes.c_char_p, ctypes.POINTER(ctypes.c_int)]


def make_nonce(conv, seq, direction):
    """构造 12B ChaCha20-Poly1305 nonce, 与 C++ 端 make_nonce 完全一致:
       [conv 4B LE][seq 4B LE][dir 1B][3B 0]"""
    nonce = bytearray(XX20_NONCE_LEN)
    nonce[0:4] = struct.pack('<I', conv & 0xFFFFFFFF)
    nonce[4:8] = struct.pack('<I', seq & 0xFFFFFFFF)
    nonce[8]   = direction
    return bytes(nonce)


def xx20_encrypt(key, nonce, plaintext):
    """ChaCha20-Poly1305 IETF 加密。返回 密文 + 16B tag (tag 附尾, 与服务端 wire 一致)。
       key 32B, nonce 12B; 无 AAD。"""
    ctx = _libcrypto.EVP_CIPHER_CTX_new()
    try:
        if _libcrypto.EVP_EncryptInit_ex(ctx, _libcrypto.EVP_chacha20_poly1305(), None, None, None) != 1:
            raise RuntimeError("EncryptInit(cipher) failed")
        if _libcrypto.EVP_CIPHER_CTX_ctrl(ctx, _EVP_CTRL_AEAD_SET_IVLEN, XX20_NONCE_LEN, None) != 1:
            raise RuntimeError("set ivlen failed")
        if _libcrypto.EVP_EncryptInit_ex(ctx, None, None, key, nonce) != 1:
            raise RuntimeError("EncryptInit(key/iv) failed")
        out = ctypes.create_string_buffer(len(plaintext) + 16)
        outlen = ctypes.c_int(0)
        if _libcrypto.EVP_EncryptUpdate(ctx, out, ctypes.byref(outlen), plaintext, len(plaintext)) != 1:
            raise RuntimeError("EncryptUpdate failed")
        cipher = out.raw[:outlen.value]
        fin = ctypes.create_string_buffer(16)
        finlen = ctypes.c_int(0)
        if _libcrypto.EVP_EncryptFinal_ex(ctx, fin, ctypes.byref(finlen)) != 1:
            raise RuntimeError("EncryptFinal failed")
        cipher += fin.raw[:finlen.value]
        tag = ctypes.create_string_buffer(16)
        if _libcrypto.EVP_CIPHER_CTX_ctrl(ctx, _EVP_CTRL_AEAD_GET_TAG, XX20_TAG_LEN, tag) != 1:
            raise RuntimeError("get tag failed")
        return cipher + tag.raw[:XX20_TAG_LEN]
    finally:
        _libcrypto.EVP_CIPHER_CTX_free(ctx)


def xx20_decrypt(key, nonce, body):
    """ChaCha20-Poly1305 IETF 解密。body = 密文 + 16B tag。验签失败返回 None。"""
    if len(body) < XX20_TAG_LEN:
        return None
    cipher, tag = body[:-XX20_TAG_LEN], body[-XX20_TAG_LEN:]
    ctx = _libcrypto.EVP_CIPHER_CTX_new()
    try:
        if _libcrypto.EVP_DecryptInit_ex(ctx, _libcrypto.EVP_chacha20_poly1305(), None, None, None) != 1:
            raise RuntimeError("DecryptInit(cipher) failed")
        if _libcrypto.EVP_CIPHER_CTX_ctrl(ctx, _EVP_CTRL_AEAD_SET_IVLEN, XX20_NONCE_LEN, None) != 1:
            raise RuntimeError("set ivlen failed")
        if _libcrypto.EVP_DecryptInit_ex(ctx, None, None, key, nonce) != 1:
            raise RuntimeError("DecryptInit(key/iv) failed")
        out = ctypes.create_string_buffer(len(cipher) + 16)
        outlen = ctypes.c_int(0)
        if _libcrypto.EVP_DecryptUpdate(ctx, out, ctypes.byref(outlen), cipher, len(cipher)) != 1:
            raise RuntimeError("DecryptUpdate failed")
        plain = out.raw[:outlen.value]
        tagbuf = ctypes.create_string_buffer(tag, XX20_TAG_LEN)
        if _libcrypto.EVP_CIPHER_CTX_ctrl(ctx, _EVP_CTRL_AEAD_SET_TAG, XX20_TAG_LEN, tagbuf) != 1:
            raise RuntimeError("set tag failed")
        fin = ctypes.create_string_buffer(16)
        finlen = ctypes.c_int(0)
        if _libcrypto.EVP_DecryptFinal_ex(ctx, fin, ctypes.byref(finlen)) <= 0:
            return None   # tag 验证失败 (被篡改 / 密钥错)
        return plain + fin.raw[:finlen.value]
    finally:
        _libcrypto.EVP_CIPHER_CTX_free(ctx)


def pack_pk(conv, pk_id, src_id, pk_seq, pk_dst_id, payload, tx_key=None):
    """组包。header 始终明文; tx_key 为 None (握手前) 时 payload 也明文,
       否则用会话 tx_key 加密 payload (上行 DIR_C2S), 密文后附 16B tag。
       header 字段顺序与 C++ Package 一致: id, src_id, dst_id, seq。"""
    body = xx20_encrypt(tx_key, make_nonce(conv, pk_seq, DIR_C2S), payload) if tx_key else payload
    return struct.pack(HEADER_FMT, pk_id, src_id, pk_dst_id, pk_seq) + body


def unpack_pk(conv, data, rx_key=None):
    """返回 (pk_id, src_id, pk_dst_id, pk_seq, payload) 或 None (半包 / 验签失败)。
       rx_key 为 None (握手前, 如 REGIST_RSP) 时 payload 明文,
       否则用会话 rx_key 解密 (下行 DIR_S2C); body = 密文 + 16B tag。"""
    if len(data) < HEADER_SIZE:
        return None
    pk_id, src_id, pk_dst_id, pk_seq = struct.unpack(HEADER_FMT, data[:HEADER_SIZE])
    body = data[HEADER_SIZE:]
    if rx_key and body:
        payload = xx20_decrypt(rx_key, make_nonce(conv, pk_seq, DIR_S2C), body)
        if payload is None:
            return None   # 验签失败, 丢弃
    else:
        payload = body
    return (pk_id, src_id, pk_dst_id, pk_seq, payload)


# ===== Envelope MAC (SipHash-2-4) =====
# UDP wire = [SipHash tag 8B][KCP frame]。信封现在由 libkcp.so 的 ikcp_output 自己加、
# ikcp_input 自己剥(见 src/kcp/ikcp.c),客户端只需把 key 通过 ikcp_set_siphash 设进去。
# key 来自 config.yml 的 kcp.siphash (明文 16 字符, C++ 端直接 memcpy 字节, 不 base64).
SH_KEY = b"XA1,y9Mn]0+iu2Y9"
assert len(SH_KEY) == 16
ENVELOPE_MAC_LEN = 8


# ===== 鉴权材料 (由 /tmp/gen_tokens.py 预生成, 写死) =====
# 固定的 client X25519 密钥对: 私钥握手时做 ECDH, 公钥已被登录服签进每个 token。
CLI_SK = base64.b64decode("EdJdIDQFPrLTOP7ppHoZi3VOrFqVWKG/e02D5pCn5IA=")
CLI_PK = base64.b64decode("IeXygWC1oAuSDeZp76WiWTkAj/VvWqs+NJ043/bG2Bo=")

# ----- token 生成所需的密钥 -----
# 网关 x25519 公钥 (sealedbox 加密 token), 取自 examples/kcp_echo/config.yml
import yaml as _yaml
with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'config.yml')) as _f:
    _KCP_CFG = _yaml.safe_load(_f)['kcp']
GW_X25519_PK = base64.b64decode(_KCP_CFG['x25519_pk'])
assert len(GW_X25519_PK) == 32
# 登录服 ed25519 私钥 (签 token)。生产在登录服; 这里取 config.yml 里被注释的那把, 仅测试用。
ED25519_SK = base64.b64decode(
    "49snRJko0ayMemUHsZ5c7qj6X0Iq09np7NQBu6njl7w6O/FaWuWLST4QN43BYMwxPJdale2LNDKJ+ry2f5sFyQ==")
assert len(ED25519_SK) == 64

# libsodium: ed25519 detached 签名 + sealedbox(crypto_box_seal) 加密
_sodium = ctypes.CDLL('libsodium.so.23')
_sodium.crypto_sign_detached.argtypes = [c_void_p, c_void_p, c_void_p, ctypes.c_ulonglong, c_void_p]
_sodium.crypto_box_seal.argtypes      = [c_void_p, c_void_p, ctypes.c_ulonglong, c_void_p]
_SEALBYTES = 48   # crypto_box_SEALBYTES

def make_token(conv, user_id, ip=0, expire=None):
    """构造 + ed25519 签名 + sealedbox 加密一个 Token, 返回 REGIST_REQ 的 payload(164B)。
       Token = expire u64 | conv u32 | user_id u32 | ip u32 | cli_pk[32] | sign[64] (小端 raw struct,
       与 C++ core::Token 一致)。sign 覆盖前 52B [expire..cli_pk](= C++ offsetof(Token, sign))。"""
    if expire is None:
        expire = int(time.time()) + 10 * 365 * 86400          # 10 年
    signed = struct.pack('<QIII', expire, conv, user_id, ip) + CLI_PK     # 52B
    sig = ctypes.create_string_buffer(64)
    assert _sodium.crypto_sign_detached(sig, None, signed, len(signed), ED25519_SK) == 0
    token = signed + sig.raw[:64]                                         # 116B
    sealed = ctypes.create_string_buffer(len(token) + _SEALBYTES)
    assert _sodium.crypto_box_seal(sealed, token, len(token), GW_X25519_PK) == 0
    return sealed.raw[:len(token) + _SEALBYTES]                           # 164B


def x25519(scalar, point):
    """RFC 7748 X25519 标量乘, 纯 Python (无第三方依赖)。返回 32B 共享点。"""
    P = 2**255 - 19
    k = bytearray(scalar); k[0] &= 248; k[31] &= 127; k[31] |= 64
    k = int.from_bytes(k, 'little')
    u = bytearray(point); u[31] &= 127
    x1 = int.from_bytes(u, 'little')
    x2, z2, x3, z3, swap = 1, 0, x1, 1, 0
    for t in range(254, -1, -1):
        kt = (k >> t) & 1
        swap ^= kt
        if swap: x2, x3 = x3, x2; z2, z3 = z3, z2
        swap = kt
        A = (x2 + z2) % P; AA = A*A % P
        B = (x2 - z2) % P; BB = B*B % P
        E = (AA - BB) % P
        C = (x3 + z3) % P; D = (x3 - z3) % P
        DA = D*A % P; CB = C*B % P
        x3 = (DA + CB)**2 % P
        z3 = x1 * (DA - CB)**2 % P
        x2 = AA*BB % P
        z2 = E * ((AA + 121665*E) % P) % P
    if swap: x2, x3 = x3, x2; z2, z3 = z3, z2
    return ((x2 * pow(z2, P-2, P)) % P).to_bytes(32, 'little')


def kx_client(srv_pk):
    """等价 libsodium crypto_kx_client_session_keys(CLI_PK, CLI_SK, srv_pk)。
       返回 (rx_key, tx_key), 各 32B (ChaCha20-Poly1305 用满 32B)。
       rx = 收 (== server 的 tx), tx = 发 (== server 的 rx)。"""
    q = x25519(CLI_SK, srv_pk)
    h = hashlib.blake2b(q + CLI_PK + srv_pk, digest_size=64).digest()
    return h[:32], h[32:]


_lib_name = 'libkcp.dll' if sys.platform == 'win32' else 'libkcp.so'
_lib = ctypes.CDLL(os.path.join(os.path.dirname(os.path.abspath(__file__)), _lib_name))

OutputFn = CFUNCTYPE(c_int, c_void_p, c_int, c_void_p, c_void_p)

_lib.ikcp_create.argtypes    = [c_uint, c_void_p];        _lib.ikcp_create.restype    = c_void_p
_lib.ikcp_release.argtypes   = [c_void_p];                _lib.ikcp_release.restype   = None
_lib.ikcp_setoutput.argtypes = [c_void_p, OutputFn];      _lib.ikcp_setoutput.restype = None
_lib.ikcp_send.argtypes      = [c_void_p, c_void_p, c_int];  _lib.ikcp_send.restype  = c_int
_lib.ikcp_recv.argtypes      = [c_void_p, c_void_p, c_int];  _lib.ikcp_recv.restype  = c_int
_lib.ikcp_input.argtypes     = [c_void_p, c_void_p, c_long]; _lib.ikcp_input.restype = c_int
_lib.ikcp_update.argtypes    = [c_void_p, c_uint];        _lib.ikcp_update.restype    = c_int
_lib.ikcp_flush.argtypes     = [c_void_p];                _lib.ikcp_flush.restype     = None
_lib.ikcp_nodelay.argtypes   = [c_void_p, c_int, c_int, c_int, c_int]; _lib.ikcp_nodelay.restype = c_int
_lib.ikcp_wndsize.argtypes   = [c_void_p, c_int, c_int];  _lib.ikcp_wndsize.restype   = c_int
_lib.ikcp_setmtu.argtypes    = [c_void_p, c_int];         _lib.ikcp_setmtu.restype    = c_int
_lib.ikcp_set_siphash.argtypes = [c_void_p, c_void_p];    _lib.ikcp_set_siphash.restype = None


def now_ms():
    return ctypes.c_uint(time.monotonic_ns() // 1_000_000).value


class Stats:
    def __init__(self):
        self.lock = threading.Lock()
        # 累计（贯穿全程）
        self.total_sent    = 0
        self.total_success = 0
        self.total_fail    = 0
        self.inflight      = 0      # 已发出但还没回 echo / 还没超时 (未响应), gauge 非累计
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
            self.inflight   += 1

    def record_success(self, latency_ms):
        with self.lock:
            self.total_success += 1
            self.iv_succ       += 1
            self.inflight      -= 1
            self.all_latencies.append(latency_ms)
            self.iv_lat_sum    += latency_ms
            self.iv_lat_count  += 1

    def record_fail(self):
        with self.lock:
            self.total_fail += 1
            self.iv_fail    += 1
            self.inflight   -= 1

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
            s = (self.iv_sent, self.iv_succ, self.iv_fail, self.inflight,
                 self.iv_lat_sum, self.iv_lat_count,
                 self.iv_bytes_out, self.iv_bytes_in)
            self.iv_sent = self.iv_succ = self.iv_fail = 0
            self.iv_lat_sum = 0.0
            self.iv_lat_count = 0
            self.iv_bytes_out = 0
            self.iv_bytes_in  = 0
            return s


def fmt_elapsed(sec):
    """运行时长自适应单位: s → m → h → d, 保留两级精度便于阅读。"""
    sec = int(sec)
    if sec < 60:
        return f'{sec}s'
    if sec < 3600:
        return f'{sec // 60}m{sec % 60:02d}s'
    if sec < 86400:
        return f'{sec // 3600}h{(sec % 3600) // 60:02d}m'
    return f'{sec // 86400}d{(sec % 86400) // 3600:02d}h'


def run_client(client_id, stats, stop_event):
    """模拟 MMO 客户端: 先做鉴权握手 (REGIST_REQ -> RSP -> ECDH 派生会话密钥),
    握手成功后固定频率发**加密**业务包并验证 echo。
    配对: server echo 经 s->send 会重写 Package.seq, 故用 payload 内嵌的
    client seq (前 4B) 来关联请求/响应, 不依赖回包 seq。"""
    conv        = 2000 + client_id
    user_id     = 90000 + client_id        # user_id 与 conv 解耦; 鉴权/路由用它(签进 token + Package.src_id)
    token       = make_token(conv, user_id)
    server_addr = (SERVER_HOST, SERVER_PORT)
    sock        = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(0.002)              # 2ms 非阻塞读，让循环及时跑 send/update

    kcp = _lib.ikcp_create(conv, None)
    _lib.ikcp_wndsize(kcp, 128, 128)
    _lib.ikcp_nodelay(kcp, 1, 10, 3, 1)
    # KCP mtu = UDP_MTU(1450) - ENVELOPE_MAC_LEN(8) = 1442 (见 core/typhon.in.hpp)
    _lib.ikcp_setmtu(kcp, 1442)
    _lib.ikcp_set_siphash(kcp, SH_KEY)     # 信封 MAC 现在由 ikcp_output 自己加, 客户端不再手动 prepend

    def output(buf, length, _kcp, _user):
        # frame 已是 [8B 信封][KCP datagram] (ikcp_output 加好), 直接发
        frame = ctypes.string_at(buf, length)
        sock.sendto(frame, server_addr)
        stats.record_bytes_out(length)
        return length
    cb = OutputFn(output)
    _lib.ikcp_setoutput(kcp, cb)

    rbuf = ctypes.create_string_buffer(65536)

    seq_ctr = [0]
    def next_seq():
        seq_ctr[0] += 1            # client 单调递增, 必须 ≠ 0; REGIST_REQ 占 1
        return seq_ctr[0]

    def pump_recv(rx_key, on_msg):
        """收一波 UDP -> ikcp_input -> drain ikcp_recv -> on_msg(parsed)。"""
        try:
            data, _ = sock.recvfrom(2048)
            stats.record_bytes_in(len(data))
            # 整段 wire(含 8B 信封)直接喂 ikcp_input, 内部自己剥信封
            ibuf = ctypes.create_string_buffer(data, len(data))
            _lib.ikcp_input(kcp, ibuf, len(data))
        except socket.timeout:
            pass
        while True:
            n = _lib.ikcp_recv(kcp, rbuf, 65536)
            if n <= 0:
                break
            parsed = unpack_pk(conv, bytes(rbuf.raw[:n]), rx_key)
            if parsed is not None:
                on_msg(parsed)

    # ---- 1. 鉴权握手 ----
    rx_key = tx_key = None
    authed = False

    def on_handshake(parsed):
        nonlocal rx_key, tx_key, authed
        pk_id, _, _, _, payload = parsed
        if pk_id == PKID_REGIST_RSP and payload and len(payload) >= 32:
            # RSP payload = server 临时 X25519 公钥 (明文)
            rx, tx = kx_client(payload[:32])
            rx_key, tx_key = rx, tx              # ChaCha20-Poly1305 用满 32B
            authed = True

    req = pack_pk(conv, PKID_REGIST_REQ, user_id, next_seq(), GATEWAY_ID, token)   # 明文, tx_key=None
    sbuf = ctypes.create_string_buffer(req, len(req))
    _lib.ikcp_send(kcp, sbuf, len(req))
    _lib.ikcp_flush(kcp)

    deadline = time.monotonic() + 5.0
    while not authed and not stop_event.is_set():
        if time.monotonic() > deadline:
            print(f'[client {client_id}] 握手超时 (conv={conv})')
            _lib.ikcp_release(kcp); sock.close()
            return
        pump_recv(None, on_handshake)        # RSP 明文, rx_key=None
        _lib.ikcp_update(kcp, now_ms())

    # ---- 2. 加密业务压测 ----
    pending   = {}                      # cseq -> (payload, t_start)
    send_interval = 1.0 / SEND_RATE_HZ  # 50ms @ 20Hz
    next_send_at  = time.monotonic()

    def on_echo(parsed):
        rcv_id, _, _, _, rcv_payload = parsed
        if rcv_id != PK_ID_PING or len(rcv_payload) < 4:
            return
        cseq = struct.unpack('<I', rcv_payload[:4])[0]
        entry = pending.pop(cseq, None)
        if entry is None:
            return              # 来历不明（重复响应 / 已超时清除）
        p_payload, p_t_start = entry
        if rcv_payload == p_payload:
            stats.record_success((time.monotonic() - p_t_start) * 1000.0)
        else:
            stats.record_fail()

    try:
        while not stop_event.is_set():
            now = time.monotonic()

            # 1. 到时间就发一包 (payload 头 4B 嵌 cseq, 用于配对)
            if now >= next_send_at:
                cseq    = next_seq()
                payload = struct.pack('<I', cseq) + os.urandom(DATA_SIZE - 4)
                pkg = pack_pk(conv, PK_ID_PING, user_id, cseq, PK_DST_ID, payload, tx_key)
                sbuf = ctypes.create_string_buffer(pkg, len(pkg))
                _lib.ikcp_send(kcp, sbuf, len(pkg))
                _lib.ikcp_flush(kcp)

                pending[cseq] = (payload, now)
                stats.record_send()

                next_send_at += send_interval
                if next_send_at < now:          # 落后太多就拉回, 避免 burst
                    next_send_at = now + send_interval

            # 2. 收 + 配对
            pump_recv(rx_key, on_echo)

            # 3. 清理超时的 pending（按 fail 算）
            if pending:
                cutoff = now - TIMEOUT_SEC
                expired = [k for k, (_, t) in pending.items() if t < cutoff]
                for k in expired:
                    pending.pop(k, None)
                    stats.record_fail()

            # 4. 推 KCP 状态机
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
            sent, succ, fail, inflight, lat_sum, lat_count, b_out, b_in = stats.snapshot_interval()
            avg     = (lat_sum / lat_count) if lat_count else 0.0
            print(f'[+{fmt_elapsed(now - t0):>6}]  发送 {sent:5d}  成功 {succ:5d}  未响应 {inflight:5d}  失败 {fail:4d}  '
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
    print(f'运行时间     {fmt_elapsed(elapsed)}')
    pending = total_sent - total_succ - total_fail
    print(f'请求统计     发送 {total_sent}   成功 {total_succ}   未响应 {pending}   失败 {total_fail}')
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
