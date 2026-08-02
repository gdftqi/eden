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

        #endregion /// 字段偏移


        #region /// 常量与限制

        /// <summary>
        /// Package 消息头长度. 共 id(16) + src_id(32) + dst_id(32) + seq(32) 112  bits / 14 Bytes
        /// </summary>
        public const int HEADER_SIZE = 10;

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
        public const int PAYLOAD_MAX = PACK_MAX_LEN - HEADER_SIZE;

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
        /// 终端注册请求 客户端主动发起(对应 Adam PID_TER_REG_REQ)
        /// </summary>
        public const ushort PID_REG_TER_REQ = 102;

        /// <summary>
        /// 终端注册应答 服务端响应(对应 Adam PID_TER_REG_RSP, payload = 32B 服务端临时公钥)
        /// </summary>
        public const ushort PID_REG_TER_RSP = 103;

        /// <summary>
        /// 请求处理失败通知(网关 -> 客户端, 对应 Adam PID_TER_ERROR).
        /// 连接仍然有效, 只是这一条请求没送到; payload 见 <see cref="DecodeError"/>.
        /// </summary>
        public const ushort PID_TER_ERROR = 114;

        /// <summary>
        /// 自定义消息ID起点: 100-199 为系统段(网关/后端专用, 客户端发送会被网关丢弃),
        /// 业务消息必须 >= 此值; 且目标后端须已声明该 PID, 否则网关会回 PID_TER_ERROR.
        /// </summary>
        public const ushort PID_CUSTOM = 200;

        #endregion /// 消息ID


        #region /// public 错误码

        // 与 Adam core/error.hpp 的 PERR_* 一一对应, 改动必须两边同步.
        // 五位数, 万位分表 / 千位分框架与业务:
        //     1xxxx  终端离开码, 由 KICK 携带 -> 连接为什么结束了
        //     2xxxx  请求失败码, 由 PID_TER_ERROR 携带 -> 连接还在, 这条请求没送到
        //     x0xxx  框架定义      x1xxx 起  业务自定义

        /// <summary>客户端主动自离</summary>
        public const uint PERR_TER_LEAVE = 10000;

        /// <summary>超时 / 网络断</summary>
        public const uint PERR_TER_DISCONNECTED = 10001;

        /// <summary>被踢(通用)</summary>
        public const uint PERR_TER_KICKED = 10002;

        /// <summary>协议违规</summary>
        public const uint PERR_TER_PROTO_ERR = 10003;

        /// <summary>登记被业务拒绝</summary>
        public const uint PERR_TER_REJECTED = 10004;

        /// <summary>网关连接断(后端本地判定)</summary>
        public const uint PERR_TER_GW_LOST = 10005;

        /// <summary>业务自定义起点(本身只作分界)</summary>
        public const uint PERR_TER_CUSTOM = 11000;

        /// <summary>顶号: 账号在其他设备登录, 可重新登录</summary>
        public const uint PERR_TER_TAKEOVER = PERR_TER_CUSTOM + 1;

        /// <summary>封号: 账号被封禁, 重登也会被拒绝</summary>
        public const uint PERR_TER_BANNED = PERR_TER_CUSTOM + 2;

        /// <summary>管理员踢下线(未封号), 可立即重新登录</summary>
        public const uint PERR_TER_ADMIN_KICK = PERR_TER_CUSTOM + 3;


        /// <summary>目标服务不可达(未发现 / 未连接 / 未鉴权) -> 可稍后重试</summary>
        public const uint PERR_REQ_UNREACHABLE = 20000;

        /// <summary>目标服务不受理该 PID -> 协议错或版本错位, 重试无用</summary>
        public const uint PERR_REQ_NOT_ACCEPT = 20001;

        /// <summary>业务自定义起点(本身只作分界)</summary>
        public const uint PERR_REQ_CUSTOM = 21000;


        /// <summary>
        /// 错误码 → 给用户看的提示语(对应 Adam 的 str_perr).
        /// 两张表合用一个方法: 号段不重叠, 不存在"查错表"这回事.
        /// </summary>
        public static string ErrorText(uint code)
        {
            switch (code)
            {
                // ---- 1xxxx 连接结束 ----
                case PERR_TER_TAKEOVER:   return "您的账号已在其他设备登录";
                case PERR_TER_BANNED:     return "您的账号已被封禁";
                case PERR_TER_ADMIN_KICK: return "您已被管理员请下线";
                case PERR_TER_REJECTED:   return "服务器拒绝了本次登录";
                case PERR_TER_PROTO_ERR:  return "客户端数据异常, 请更新版本";

                // ---- 2xxxx 请求没送到 ----
                case PERR_REQ_UNREACHABLE: return "服务暂时不可用, 请稍后重试";
                case PERR_REQ_NOT_ACCEPT:  return "客户端版本过旧, 请更新版本";
            }

            // 未知码按号段兜底: 2xxxx 连接还在, 1xxxx 及其它一律当断开处理
            if (code >= PERR_REQ_UNREACHABLE)
            {
                return "请求处理失败";
            }

            return "连接已断开";
        }

        #endregion /// public 错误码


        #region /// PID_TER_ERROR 载荷

        /// <summary>
        /// ErrorNotify 长度: code(4) + dstId(4) + pid(4), 全小端
        /// </summary>
        public const int ERROR_NOTIFY_LEN = 12;


        /// <summary>
        /// 解析 PID_TER_ERROR 的载荷.dstId/pid 回带原请求的寻址字段,
        /// 据此定位是哪一条请求失败了.
        /// </summary>
        /// <returns>长度不足返回 false</returns>
        public static bool DecodeError(Package pkg, out uint code, out uint dstId, out uint pid)
        {
            code = dstId = pid = 0;
            if (pkg.PayloadLength < ERROR_NOTIFY_LEN)
            {
                return false;
            }

            Decode32LE(pkg.Payload, 0, out code);
            Decode32LE(pkg.Payload, 4, out dstId);
            Decode32LE(pkg.Payload, 8, out pid);
            return true;
        }

        #endregion /// PID_TER_ERROR 载荷


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
            SrcID = DstID = 0;
            PayloadLength = 0;
        }
    }
}