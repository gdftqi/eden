using Lilith.Utils;
using Newtonsoft.Json;
using System;


namespace Lilith.Core
{
    /// <summary>
    /// 应用层协议
    /// </summary>
    public class Package
    {
        /// <summary>
        /// 16 位大端序编码
        /// </summary>
        /// <param name="p"></param>
        /// <param name="offset"></param>
        /// <param name="value"></param>
        /// <returns></returns>
        public static int Encode16BE(byte[] p, int offset, ushort value)
        {
            p[offset + 0] = (byte)(value >> 8);
            p[offset + 1] = (byte)(value >> 0);
            return 2;
        }


        /// <summary>
        /// 16 位大端序解码
        /// </summary>
        /// <param name="p"></param>
        /// <param name="offset"></param>
        /// <param name="value"></param>
        /// <returns></returns>
        public static int Decode16BE(byte[] p, int offset, out ushort value)
        {// 16 大端解码
            value = (ushort)((p[offset + 0] << 8) | p[offset + 1]);
            return 2;
        }


        /// <summary>
        /// 32 位大端序编码
        /// </summary>
        /// <param name="p"></param>
        /// <param name="offset"></param>
        /// <param name="value"></param>
        /// <returns></returns>
        public static int Encode32BE(byte[] p, int offset, uint value)
        {// 32 大端编码
            p[offset + 0] = (byte)(value >> 24);
            p[offset + 1] = (byte)(value >> 16);
            p[offset + 2] = (byte)(value >> 8);
            p[offset + 3] = (byte)(value >> 0);
            return 4;
        }


        /// <summary>
        /// 32位大端序解码
        /// </summary>
        /// <param name="p"></param>
        /// <param name="offset"></param>
        /// <param name="value"></param>
        /// <returns></returns>
        public static int Decode32BE(byte[] p, int offset, out uint value)
        {// 32 大端解码
            value = ((uint)p[offset + 0] << 24)
                   | ((uint)p[offset + 1] << 16)
                   | ((uint)p[offset + 2] << 8)
                   | ((uint)p[offset + 3] << 0);
            return 4;
        }

        #region /// 字段偏移

        /// <summary>
        /// Package.ID 偏移量 16 bits
        /// </summary>
        public const int OFFSET_ID = 0;

        /// <summary>
        /// Package.SrcID 偏移量 32 bits
        /// </summary>
        public const int OFFSET_SRC_ID = 2;

        /// <summary>
        /// Package.DstID 偏移量 32 bits
        /// </summary>
        public const int OFFSET_DST_ID = 6;

        /// <summary>
        /// Package.Idempotent 偏移量 32 bits
        /// </summary>
        public const int OFFSET_IDEM = 10;

        #endregion /// 字段偏移


        #region /// 常量与限制

        /// <summary>
        /// Package 消息头长度. 共 id(16) + src_id(32) + dst_id(32) + seq(32) 112  bits / 14 Bytes
        /// </summary>
        public const int HEADER_SIZE = 14;

        /// <summary>
        /// ChaCha20-Poly1305 Tag 长度, 加密后附在 payload 尾部
        /// </summary>
        public const int TAG_LEN = 16;

        /// <summary>
        /// 最大的 KCP 消息长度
        /// </summary>
        public const int PACK_MAX_LEN = 65535;

        /// <summary>
        /// 最大的 Payload 长度
        /// </summary>
        public const int PAYLOAD_MAX = PACK_MAX_LEN - HEADER_SIZE - TAG_LEN;

        #endregion /// 常量与限制


        private static SafePool<Package> pool = new SafePool<Package>(() => new Package(), pkg => pkg.Reset());

        /// <summary>
        /// 对象池, 线程安全
        /// </summary>
        public static SafePool<Package> Pool
        {
            get
            {
                return pool;
            }
        }


        #region /// 消息ID

        /// <summary>
        /// PING 客户端主动发起
        /// </summary>
        public const ushort PKID_PING = 100;

        /// <summary>
        /// PONG 服务端响应
        /// </summary>
        public const ushort PKID_PONG = 101;

        /// <summary>
        /// 鉴权注册请求 客户端主动发起
        /// </summary>
        public const ushort PKID_REGIST_REQ = 102;

        /// <summary>
        /// 鉴权注册应答 服务端响应
        /// </summary>
        public const ushort PKID_REGIST_RSP = 103;

        #endregion /// 消息ID


        #region /// 消息字段

        /// <summary>
        /// 消息头: 消息ID
        /// </summary>
        [JsonProperty("id")]
        public ushort ID;

        /// <summary>
        /// 消息头: 源ID
        /// </summary>
        [JsonProperty("src_id")]
        public uint SrcID;

        /// <summary>
        /// 消息头: 目标ID
        /// </summary>
        [JsonProperty("dst_id")]
        public uint DstID;

        /// <summary>
        /// 消息头: 幂等
        /// </summary>
        [JsonProperty("idempotent")]
        public uint Idempotent;

        /// <summary>
        /// 消息体
        /// </summary>
        [JsonIgnore]
        public readonly byte[] Payload = new byte[PAYLOAD_MAX];

        /// <summary>
        /// 消息体的 hex 形式, 只截有效长度(PayloadLength), 仅供 ToString/日志用
        /// </summary>
        [JsonProperty("payload")]
        private string PayloadHex
        {
            get { return BitConverter.ToString(Payload, 0, PayloadLength).Replace("-", ""); }
        }

        #endregion /// 消息字段


        #region /// 附加字段

        /// <summary>
        /// Payload 长度
        /// </summary>
        [JsonIgnore]
        public int PayloadLength;


        /// <summary>
        /// Package总长度
        /// </summary>
        [JsonIgnore]
        public int PkLen
        {
            get
            {
                return HEADER_SIZE + PayloadLength;
            }
        }

        #endregion /// 附加字段


        /// <summary>
        /// JSON 格式化
        /// </summary>
        /// <returns></returns>
        public override string ToString()
        {
            return JsonConvert.SerializeObject(this);
        }


        /// <summary>
        /// 重置
        /// </summary>
        public void Reset()
        {
            ID = 0;
            SrcID = DstID = Idempotent = 0;
            PayloadLength = 0;
        }
    }
}