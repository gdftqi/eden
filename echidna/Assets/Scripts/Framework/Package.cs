using kcp2k;
using System;


namespace Echidna
{
    // =========================================================================
    //                       typhon 消息协议(v1) —— C# 客户端镜像
    //                必须与 C++ include/package.hpp 保持严格一致
    // =========================================================================
    //
    // 字节序:所有多字节字段一律 big-endian(网络字节序)。
    // 单包上限:PkLen 是 uint16 → 单条 KCP message 最大 65535 字节(含 12B 头)。
    //
    // -------------------------------------------------------------------------
    //  Package(客户端 ↔ 网关,KCP 上跑的就是这个)
    // -------------------------------------------------------------------------
    //   0                   1                   2                   3
    //   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
    //  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    //  |             pk_len            |             pk_id             |
    //  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    //  |                           pk_idem                             |
    //  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    //  |                          pk_dst_id                            |
    //  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    //  |                    pk_data ... (variable)                     |
    //  +---------------------------------------------------------------+
    //
    //   pk_len    包总长(含 12B 头 + pk_data;**不含**网关追加的 PackageTail)
    //   pk_id     业务消息号
    //   pk_idem   幂等 ID(客户端单调递增,必须 != 0)
    //   pk_dst_id 目标服务类型路由键
    //
    // -------------------------------------------------------------------------
    //  PackageTail(网关 → 业务服)—— **客户端不接触**,仅作文档
    // -------------------------------------------------------------------------
    //  pkt_src_id   = FromPlayerID(网关 stamp,不可伪造)
    //  pkt_src_addr = 客户端 IPv4 地址
    //
    // =========================================================================


    /// <summary>
    /// 一条 Package。**class 类型,可放入对象池复用**:
    ///   var pkg = pool.Rent();    // 或 new Package()
    ///   pkg.Reset();
    ///   pkg.PkId    = Package.PK_ID_PING;
    ///   pkg.PkDstId = 0;
    ///   // 把 payload 写到 pkg.Payload[0..N]
    ///   pkg.PayloadLength = N;
    ///   session.SendPk(pkg);
    ///   pool.Return(pkg);
    ///
    /// 自身持有一个 PAYLOAD_MAX 大小的 byte[],复用过程中不再分配。
    /// PkIdem 由 KcpSession.SendPk 自动 stamp,调用方无需关心。
    /// </summary>
    public class Package
    {
        // ---- 字段偏移 / 大小 ----
        public const int OFFSET_LEN    = 0;
        public const int OFFSET_ID     = 2;
        public const int OFFSET_IDEM   = 4;
        public const int OFFSET_DST_ID = 8;
        public const int HEADER_SIZE   = 12;
        public const int PACK_MAX_LEN  = 65535;
        public const int PAYLOAD_MAX   = PACK_MAX_LEN - HEADER_SIZE;

        // ---- 业务消息号约定(必须与 C++ 端保持一致)----
        public const ushort PK_ID_PING    = 1;
        public const ushort PK_ID_MOVE    = 100;
        public const ushort PK_ID_BUY_REQ = 200;


        // ---- 字段(host order) ----
        public ushort PkId;
        public uint   PkIdem;
        public uint   PkDstId;

        // ---- payload 缓冲:owned,固定 PAYLOAD_MAX 长,跨池化周期复用 ----
        public readonly byte[] Payload = new byte[PAYLOAD_MAX];
        public int             PayloadLength;

        // ---- 派生 ----
        public int PkLen => HEADER_SIZE + PayloadLength;


        /// <summary>清空字段(Payload 内的字节保持原状,下次写入时被覆盖)。</summary>
        public void Reset()
        {
            PkId          = 0;
            PkIdem        = 0;
            PkDstId       = 0;
            PayloadLength = 0;
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
            p[offset + 2] = (byte)(value >>  8);
            p[offset + 3] = (byte)(value >>  0);
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
            value =  ((uint)p[offset + 0] << 24)
                   | ((uint)p[offset + 1] << 16)
                   | ((uint)p[offset + 2] <<  8)
                   | ((uint)p[offset + 3] <<  0);
            return 4;
        }


        /// <summary>
        /// 把 pkg 序列化到 wireBuf(从 index 0 写起)。
        /// pkg.PkIdem 必须已经被 stamp(!= 0),通常由 KcpSession.SendPk 设置。
        /// </summary>
        /// <returns>写入字节数 = HEADER_SIZE + pkg.PayloadLength</returns>
        public static int Pack(Package pkg, byte[] wireBuf)
        {
            if (pkg.PkIdem == 0)
                throw new ArgumentException("PkIdem must be != 0");

            int total = HEADER_SIZE + pkg.PayloadLength;
            if (total > PACK_MAX_LEN)
                throw new ArgumentException("package too large: " + total);
            if (wireBuf.Length < total)
                throw new ArgumentException("wireBuf too small");

            Encode16BE(wireBuf, OFFSET_LEN,    (ushort)total);
            Encode16BE(wireBuf, OFFSET_ID,     pkg.PkId);
            Encode32BE(wireBuf, OFFSET_IDEM,   pkg.PkIdem);
            Encode32BE(wireBuf, OFFSET_DST_ID, pkg.PkDstId);

            if (pkg.PayloadLength > 0)
                Buffer.BlockCopy(pkg.Payload, 0, wireBuf, HEADER_SIZE, pkg.PayloadLength);

            return total;
        }


        /// <summary>
        /// 从 wireBuf[0..len) 解析一条完整包,写入 pkg。
        /// pkg 由调用方提供(便于池化复用);成功 / 失败 都不重置 pkg,失败请自行 Reset。
        /// payload 会被拷贝到 pkg.Payload。
        /// </summary>
        /// <returns>true = 成功;false = 长度不匹配 / idem == 0 等非法包</returns>
        public static bool Unpack(byte[] wireBuf, int len, Package pkg)
        {
            if (len < HEADER_SIZE) return false;

            ushort pkLen;
            Decode16BE(wireBuf, OFFSET_LEN, out pkLen);
            if (pkLen != len) return false;

            Decode16BE(wireBuf, OFFSET_ID,     out pkg.PkId);
            Decode32BE(wireBuf, OFFSET_IDEM,   out pkg.PkIdem);
            Decode32BE(wireBuf, OFFSET_DST_ID, out pkg.PkDstId);
            if (pkg.PkIdem == 0) return false;

            pkg.PayloadLength = len - HEADER_SIZE;
            if (pkg.PayloadLength > 0)
                Buffer.BlockCopy(wireBuf, HEADER_SIZE, pkg.Payload, 0, pkg.PayloadLength);
            return true;
        }

        // 进程级单例池,主线程使用(kcp2k.Pool 内部用 Stack,不是 thread-safe)。
        // 首次访问时懒初始化,预分配 16 个实例覆盖典型帧内峰值。
        private static Pool<Package> pool;
        public static Pool<Package> Pool
        {
            get
            {
                if (pool == null)
                    pool = new Pool<Package>(() => new Package(), pkg => pkg.Reset(), 16);
                return pool;
            }
        }
    }
}
