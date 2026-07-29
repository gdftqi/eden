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
        /// 16 位小端序编码(与 C++ u16_to_le 对齐)
        /// </summary>
        public static int Encode16LE(byte[] p, int offset, ushort value)
        {
            p[offset + 0] = (byte)(value >> 0);
            p[offset + 1] = (byte)(value >> 8);
            return 2;
        }


        /// <summary>
        /// 16 位小端序解码
        /// </summary>
        public static int Decode16LE(byte[] p, int offset, out ushort value)
        {
            value = (ushort)(p[offset + 0] | (p[offset + 1] << 8));
            return 2;
        }


        /// <summary>
        /// 32 位小端序编码(与 C++ u32_to_le 对齐)
        /// </summary>
        public static int Encode32LE(byte[] p, int offset, uint value)
        {
            p[offset + 0] = (byte)(value >> 0);
            p[offset + 1] = (byte)(value >> 8);
            p[offset + 2] = (byte)(value >> 16);
            p[offset + 3] = (byte)(value >> 24);
            return 4;
        }


        /// <summary>
        /// 32 位小端序解码
        /// </summary>
        public static int Decode32LE(byte[] p, int offset, out uint value)
        {
            value = ((uint)p[offset + 0] << 0)
                   | ((uint)p[offset + 1] << 8)
                   | ((uint)p[offset + 2] << 16)
                   | ((uint)p[offset + 3] << 24);
            return 4;
        }

        #region /// 字段偏移

        /// <summary>
        /// Package.PID 偏移量 16 bits
        /// </summary>
        public const int OFFSET_PID = 0;

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
        /// 终端注册请求 客户端主动发起(对应 Adam PID_REG_TER_REQ)
        /// </summary>
        public const ushort PID_REG_TER_REQ = 102;

        /// <summary>
        /// 终端注册应答 服务端响应(对应 Adam PID_REG_TER_RSP, payload = 32B 服务端临时公钥)
        /// </summary>
        public const ushort PID_REG_TER_RSP = 103;

        /// <summary>
        /// 自定义消息ID起点: 100-199 为系统段(网关/后端专用, 客户端发送会被网关丢弃),
        /// 业务消息必须 >= 此值; 且目标后端须已声明该 PID, 否则连接会被网关判死。
        /// </summary>
        public const ushort PID_CUSTOM = 200;

        #endregion /// 消息ID


        #region /// 终端离开码

        /// <summary>客户端主动自离</summary>
        public const uint TER_CODE_LEAVE = 1;

        /// <summary>超时 / 网络断</summary>
        public const uint TER_CODE_DISCONNECTED = 2;

        /// <summary>被踢(通用)</summary>
        public const uint TER_CODE_KICKED = 3;

        /// <summary>协议违规</summary>
        public const uint TER_CODE_PROTO_ERR = 4;

        /// <summary>登记被业务拒绝</summary>
        public const uint TER_CODE_REJECTED = 5;

        /// <summary>网关连接断(后端本地判定)</summary>
        public const uint TER_CODE_GW_LOST = 6;

        /// <summary>业务自定义起点(本身只作分界)</summary>
        public const uint TER_CODE_CUSTOM = 100;

        /// <summary>顶号: 账号在其他设备登录, 可重新登录</summary>
        public const uint TER_CODE_TAKEOVER = TER_CODE_CUSTOM + 1;

        /// <summary>封号: 账号被封禁, 重登也会被拒绝</summary>
        public const uint TER_CODE_BANNED = TER_CODE_CUSTOM + 2;

        /// <summary>管理员踢下线(未封号), 可立即重新登录</summary>
        public const uint TER_CODE_ADMIN_KICK = TER_CODE_CUSTOM + 3;


        /// <summary>
        /// 离开码 → 给用户看的提示语(对应 Adam 的 str_terminal_code)
        /// </summary>
        public static string TerminalCodeText(uint code)
        {
            switch (code)
            {
                case TER_CODE_TAKEOVER:   return "您的账号已在其他设备登录";
                case TER_CODE_BANNED:     return "您的账号已被封禁";
                case TER_CODE_ADMIN_KICK: return "您已被管理员请下线";
                case TER_CODE_REJECTED:   return "服务器拒绝了本次登录";
                case TER_CODE_PROTO_ERR:  return "客户端数据异常, 请更新版本";
                default:                  return "连接已断开";
            }
        }

        #endregion /// 终端离开码


        #region /// 消息字段

        /// <summary>
        /// 消息头: 消息ID
        /// </summary>
        [JsonProperty("pid")]
        public ushort PID;

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
            PID = 0;
            SrcID = DstID = Idempotent = 0;
            PayloadLength = 0;
        }
    }
}