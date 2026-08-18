using Google.Protobuf;
using Lilith.Components;
using Lilith.Core;
using Lilith.Utils;

namespace CC.Model
{
    /// <summary>
    /// 聊天业务的协议常量,
    /// </summary>
    public static class ChatProto
    {
        /// <summary>
        /// 聊天服务(CCS)的服务号
        /// </summary>
        public const uint CCS_ID = 0x20000000;

        /// <summary>
        /// 单聊发送请求(客户端 -> CCS), payload = ccs.SingleChatReq.
        /// </summary>
        public const ushort PID_SINGLE_CHAT_REQ = Package.PID_CUSTOM + 1;

        /// <summary>
        /// 单聊发送应答/ACK(CCS -> 发送方), payload = ccs.SingleChatRsp, 按 cli_id 认领.
        /// </summary>
        public const ushort PID_SINGLE_CHAT_RSP = Package.PID_CUSTOM + 2;

        /// <summary>
        /// 单聊推送(CCS -> 接收方), payload = ccs.SingleChatNtf.
        /// </summary>
        public const ushort PID_SINGLE_CHAT_NTF = Package.PID_CUSTOM + 3;

        /// <summary>
        /// 清空聊天记录请求(双边: 服务端删库, 两边都清).
        /// </summary>
        public const ushort PID_CLEAR_CHAT_REQ = Package.PID_CUSTOM + 4;

        /// <summary>
        /// 清空聊天记录应答(给发起方).
        /// </summary>
        public const ushort PID_CLEAR_CHAT_RSP = Package.PID_CUSTOM + 5;

        /// <summary>
        /// 清空聊天记录通知(给对端, 让它也清掉本地记录).
        /// </summary>
        public const ushort PID_CLEAR_CHAT_NTF = Package.PID_CUSTOM + 6;

        /// <summary>
        /// 删除会话请求(双边: 消息和会话在两边都消失).
        /// </summary>
        public const ushort PID_DELETE_CHAT_REQ = Package.PID_CUSTOM + 7;

        /// <summary>
        /// 删除会话应答(给发起方).
        /// </summary>
        public const ushort PID_DELETE_CHAT_RSP = Package.PID_CUSTOM + 8;

        /// <summary>
        /// 删除会话通知(给对端).
        /// </summary>
        public const ushort PID_DELETE_CHAT_NTF = Package.PID_CUSTOM + 9;

        /// <summary>
        /// 已读上报: 告诉服务端"我在这个会话读到 seq 了".
        /// </summary>
        public const ushort PID_CONFIRM_CHAT_REQ = Package.PID_CUSTOM + 10;

        /// <summary>
        /// 已读上报应答.
        /// </summary>
        public const ushort PID_CONFIRM_CHAT_RSP = Package.PID_CUSTOM + 11;

        /// <summary>
        /// 对端已读通知: 对方读到 seq 了, 据此把自己发的消息标成已读.
        /// </summary>
        public const ushort PID_CONFIRM_CHAT_NTF = Package.PID_CUSTOM + 12;

        /// <summary>
        /// 拉会话水位(离线同步第一步): 服务端把该用户全部会话的 last_seq 等一次给回来.
        /// </summary>
        public const ushort PID_GET_CHAT_CURSOR_REQ = Package.PID_CUSTOM + 13;

        /// <summary>
        /// 会话水位应答.
        /// </summary>
        public const ushort PID_GET_CHAT_CURSOR_RSP = Package.PID_CUSTOM + 14;

        /// <summary>
        /// 拉某个会话的历史(第二步, 点开会话时按需拉).
        /// </summary>
        public const ushort PID_GET_CHAT_MSG_REQ = Package.PID_CUSTOM + 15;

        /// <summary>
        /// 聊天记录应答.
        /// </summary>
        public const ushort PID_GET_CHAT_MSG_RSP = Package.PID_CUSTOM + 16;


        /// <summary>
        /// 序列化 pb 消息并发给 CCS: 唯一的组包出口.
        /// 直接写进包的 Payload 缓冲, 不产生中间数组; 包所有权随 Hydra.Send 移交.
        /// </summary>
        /// <returns>0 成功; -1 未连接; -2 消息超长</returns>
        public static int Send(IMessage msg, ushort pid)
        {
            if (Hydra.Instance.State != HydraState.Connected)
            {
                Log.Write($"[CC] 未连接, 丢弃 pid = {pid}");
                return -1;
            }

            int size = msg.CalculateSize();
            if (size > Package.PAYLOAD_MAX)
            {
                Log.Write($"[CC] 消息超长, 丢弃 pid = {pid}, size = {size}");
                return -2;
            }

            var pkg = Package.Pool.Take();
            pkg.PID = pid;
            pkg.DstID = CCS_ID;

            var os = new CodedOutputStream(pkg.Payload);
            msg.WriteTo(os);
            os.Flush();
            pkg.PayloadLength = size;

            Hydra.Instance.Send(pkg);
            return 0;
        }
    }
}
