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
        public const int OFFSET_SRC_ID = 2;
        public const int OFFSET_DST_ID = 6;
        public const int OFFSET_SEQ = 10;
        public const int HEADER_SIZE = 14;     // sizeof(Package): id(2)+src_id(4)+dst_id(4)+seq(4)
        public const int TAG_LEN = 16;     // ChaCha20-Poly1305 tag, 加密后附在 payload 尾
        public const int PACK_MAX_LEN = 65535;  // wire frame 上限 (任意方向)
        public const int PAYLOAD_MAX = PACK_MAX_LEN - HEADER_SIZE - TAG_LEN;

        public const ushort PKID_PING = 100;  // PING
        public const ushort PKID_PONG = 101;  // PONG
        public const ushort PKID_REGIST_REQ = 102;  // 鉴权注册请求
        public const ushort PKID_REGIST_RSP = 103;  // 鉴权注册应答

        // ---- 字段 (host order) ----
        public ushort PkId;
        public uint PkSrcId;   // 源 id (= user_id); 网关据此校验 token.user_id
        public uint PkDstId;
        public uint PkSeq;

        // ---- payload 缓冲: owned, 固定 PAYLOAD_MAX 长, 跨池化周期复用 ----
        public readonly byte[] Payload = new byte[PAYLOAD_MAX];
        public int PayloadLength;

        // ---- 派生 ----
        public int PkLen => HEADER_SIZE + PayloadLength;

        public void Reset()
        {
            PkId = 0;
            PkSrcId = PkDstId = PkSeq = 0;
            PayloadLength = 0;
        }

        public void CopyFrom(Package src)
        {
            PkId = src.PkId;
            PkSrcId = src.PkSrcId;
            PkDstId = src.PkDstId;
            PkSeq = src.PkSeq;
            PayloadLength = src.PayloadLength;
            if (src.PayloadLength > 0)
            {
                Buffer.BlockCopy(src.Payload, 0, Payload, 0, src.PayloadLength);
            }
        }

        public static int Encode16BE(byte[] p, int offset, ushort value)
        {// 16 大端编码
            p[offset + 0] = (byte)(value >> 8);
            p[offset + 1] = (byte)(value >> 0);
            return 2;
        }

        public static int Decode16BE(byte[] p, int offset, out ushort value)
        {// 16 大端解码
            value = (ushort)((p[offset + 0] << 8) | p[offset + 1]);
            return 2;
        }

        public static int Encode32BE(byte[] p, int offset, uint value)
        {// 32 大端编码
            p[offset + 0] = (byte)(value >> 24);
            p[offset + 1] = (byte)(value >> 16);
            p[offset + 2] = (byte)(value >> 8);
            p[offset + 3] = (byte)(value >> 0);
            return 4;
        }

        public static int Decode32BE(byte[] p, int offset, out uint value)
        {// 32 大端解码
            value = ((uint)p[offset + 0] << 24)
                   | ((uint)p[offset + 1] << 16)
                   | ((uint)p[offset + 2] << 8)
                   | ((uint)p[offset + 3] << 0);
            return 4;
        }

        public static int Pack(Package pkg, byte[] wireBuf)
        {// 装包
            if (pkg.PkSeq == 0)
            {
                throw new ArgumentException("PkSeq must be != 0");
            }

            if (pkg.PayloadLength > PAYLOAD_MAX)
            {
                throw new ArgumentException("payload too large: " + pkg.PayloadLength);
            }

            int total = HEADER_SIZE + pkg.PayloadLength;
            if (wireBuf.Length < total)
            {
                throw new ArgumentException("wireBuf too small");
            }

            Encode16BE(wireBuf, OFFSET_ID, pkg.PkId);
            Encode32BE(wireBuf, OFFSET_SRC_ID, pkg.PkSrcId);
            Encode32BE(wireBuf, OFFSET_DST_ID, pkg.PkDstId);
            Encode32BE(wireBuf, OFFSET_SEQ, pkg.PkSeq);

            if (pkg.PayloadLength > 0)
            {
                Buffer.BlockCopy(pkg.Payload, 0, wireBuf, HEADER_SIZE, pkg.PayloadLength);
            }

            return total;
        }

        public static bool Unpack(byte[] wireBuf, int len, Package pkg)
        {// 解包
            if (len < HEADER_SIZE)
            {
                return false;
            }

            Decode16BE(wireBuf, OFFSET_ID, out pkg.PkId);
            Decode32BE(wireBuf, OFFSET_SRC_ID, out pkg.PkSrcId);
            Decode32BE(wireBuf, OFFSET_DST_ID, out pkg.PkDstId);
            Decode32BE(wireBuf, OFFSET_SEQ, out pkg.PkSeq);
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