namespace CC
{
    public enum MsgID
    {
        None = 0,

        /// <summary>部门创建成功.Param = Department</summary>
        DeptCreated,

        /// <summary>成员创建成功.Param = 用户名</summary>
        MemberCreated,

        /// <summary>打开与某人的会话.Param = 对端 User</summary>
        OpenChat,
    }


    public sealed class Message
    {
        public MsgID ID { get; init; }

        /// <summary>附加参数.运行时拆箱, 类型由消息号约定(见 MsgID 上的注释)</summary>
        public object? Param { get; init; }

        /// <summary>发送者.广播时用来跳过自己, 别的地方一般用不上</summary>
        public object? Sender { get; init; }

        public Message(MsgID id, object? param = null, object? sender = null)
        {
            ID = id;
            Param = param;
            Sender = sender;
        }
    }
}
