using lilith.Utils;
using System;


namespace lilith.Core
{
    // =========================================================================
    //              typhon 消息协议 (v2, 10B 头) —— C# 客户端镜像
    //          必须与 C++ include/core/package.hpp 保持严格一致
    // =========================================================================
    //
    // 字节序: 所有多字节字段一律 big-endian(网络字节序)。
    //
    //   0                   1                   2                   3
    //   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
    //  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    //  |             pk_id            |            pk_seq (hi)         |
    //  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    //  |     pk_seq (lo)             |          pk_dst_id (hi)         |
    //  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    //  | pk_dst_id (lo)|             pk_payload ... (variable)        |
    //  +---------------------------------------------------------------+
    //
    //   pk_id     业务消息号 (2B)
    //   pk_seq    序号 (4B), 客户端单调递增, 必须 != 0; 兼作 ChaCha20 nonce 输入
    //   pk_dst_id 目标服务 id (4B), 路由键
    //   pk_payload  业务数据; **不带长度字段**, 长度由 KCP 消息边界给定
    //
    // 注意: Package 自身只描述**明文**结构。authed 之后 payload 会被
    // ChaCha20-Poly1305 加密、尾部附 16B tag —— 那一层在 KcpSession 里处理,
    // 本类的 Pack/Unpack 只负责明文 header + payload 的编解码。
    // =========================================================================
    public class Package
    {
        // ---- 字段偏移 / 大小 ----
        public const int OFFSET_ID = 0;
        public const int OFFSET_SEQ = 2;
        public const int OFFSET_DST_ID = 6;
        public const int HEADER_SIZE = 10;     // sizeof(Package)
        public const int TAG_LEN = 16;     // ChaCha20-Poly1305 tag, 加密后附在 payload 尾
        public const int PACK_MAX_LEN = 65535;  // wire frame 上限 (任意方向)
        public const int PAYLOAD_MAX = PACK_MAX_LEN - HEADER_SIZE - TAG_LEN;

        public const ushort PKID_PING = 100;  // PING
        public const ushort PKID_PONG = 101;  // PONG
        public const ushort PKID_REGIST_REQ = 102;  // 鉴权注册请求
        public const ushort PKID_REGIST_RSP = 103;  // 鉴权注册应答

        // ---- 字段 (host order) ----
        public ushort PkId;
        public uint PkSeq;
        public uint PkDstId;

        // ---- payload 缓冲: owned, 固定 PAYLOAD_MAX 长, 跨池化周期复用 ----
        public readonly byte[] Payload = new byte[PAYLOAD_MAX];
        public int PayloadLength;

        // ---- 派生 ----
        public int PkLen => HEADER_SIZE + PayloadLength;


        /// <summary>清空字段 (Payload 内字节保持原状, 下次写入时覆盖)。</summary>
        public void Reset()
        {
            PkId = 0;
            PkSeq = 0;
            PkDstId = 0;
            PayloadLength = 0;
        }


        /// <summary>
        /// 深拷贝 src 的字段 + payload 到本对象。用于把调用方的 Package 拷成一份
        /// 独占副本跨线程投递, 拷完调用方即可复用/归还原对象。
        /// </summary>
        public void CopyFrom(Package src)
        {
            PkId = src.PkId;
            PkSeq = src.PkSeq;
            PkDstId = src.PkDstId;
            PayloadLength = src.PayloadLength;
            if (src.PayloadLength > 0)
                Buffer.BlockCopy(src.Payload, 0, Payload, 0, src.PayloadLength);
        }


        // ---- big-endian 编码 ----
        public static int Encode16BE(byte[] p, int offset, ushort value)
        {
            p[offset + 0] = (byte)(value >> 8);
            p[offset + 1] = (byte)(value >> 0);
            return 2;
        }

        public static int Encode32BE(byte[] p, int offset, uint value)
        {
            p[offset + 0] = (byte)(value >> 24);
            p[offset + 1] = (byte)(value >> 16);
            p[offset + 2] = (byte)(value >> 8);
            p[offset + 3] = (byte)(value >> 0);
            return 4;
        }

        // ---- big-endian 解码 ----
        public static int Decode16BE(byte[] p, int offset, out ushort value)
        {
            value = (ushort)((p[offset + 0] << 8) | p[offset + 1]);
            return 2;
        }

        public static int Decode32BE(byte[] p, int offset, out uint value)
        {
            value = ((uint)p[offset + 0] << 24)
                   | ((uint)p[offset + 1] << 16)
                   | ((uint)p[offset + 2] << 8)
                   | ((uint)p[offset + 3] << 0);
            return 4;
        }


        /// <summary>
        /// 把 pkg 的**明文** header + payload 序列化到 wireBuf(从 index 0)。
        /// pkg.PkSeq 必须已 stamp(!= 0), 通常由 KcpSession.SendPk 设置。
        /// 加密 / tag 由调用方(KcpSession)在此基础上叠加。
        /// </summary>
        /// <returns>写入字节数 = HEADER_SIZE + pkg.PayloadLength</returns>
        public static int Pack(Package pkg, byte[] wireBuf)
        {
            if (pkg.PkSeq == 0)
                throw new ArgumentException("PkSeq must be != 0");
            if (pkg.PayloadLength > PAYLOAD_MAX)
                throw new ArgumentException("payload too large: " + pkg.PayloadLength);

            int total = HEADER_SIZE + pkg.PayloadLength;
            if (wireBuf.Length < total)
                throw new ArgumentException("wireBuf too small");

            Encode16BE(wireBuf, OFFSET_ID, pkg.PkId);
            Encode32BE(wireBuf, OFFSET_SEQ, pkg.PkSeq);
            Encode32BE(wireBuf, OFFSET_DST_ID, pkg.PkDstId);

            if (pkg.PayloadLength > 0)
                Buffer.BlockCopy(pkg.Payload, 0, wireBuf, HEADER_SIZE, pkg.PayloadLength);

            return total;
        }

        public static bool Unpack(byte[] wireBuf, int len, Package pkg)
        {// 解包
            if (len < HEADER_SIZE)
            {
                return false;
            }

            Decode16BE(wireBuf, OFFSET_ID, out pkg.PkId);
            Decode32BE(wireBuf, OFFSET_SEQ, out pkg.PkSeq);
            Decode32BE(wireBuf, OFFSET_DST_ID, out pkg.PkDstId);
            if (pkg.PkSeq == 0)
            {
                return false;
            }

            pkg.PayloadLength = len - HEADER_SIZE;
            if (pkg.PayloadLength > 0)
            {
                Buffer.BlockCopy(wireBuf, HEADER_SIZE, pkg.Payload, 0, pkg.PayloadLength);
            }

            return true;
        }


        // 进程级单例池, 主线程使用(kcp2k.Pool 内部用 Stack, 非 thread-safe)。
        private static SafePool<Package> pool = new SafePool<Package>(() => new Package(), pkg => pkg.Reset());
        public static SafePool<Package> Pool
        {
            get
            {
                return pool;
            }
        }
    }
}