using Lilith.Core;
using System;

namespace CC.Model
{
    /// <summary>
    /// 当前登录的用户
    /// </summary>
    public static class Me
    {
        public static User? User { get; private set; }


        /// <summary>
        /// 当前用户 id.没登录时是 0.
        /// </summary>
        public static Int64 ID
        {
            get { return User?.ID ?? HttpSession.Instance.UserID ?? 0; }
        }


        /// <summary>
        /// 从登录应答里取出当前用户.登录成功后调一次.
        /// </summary>
        public static void Sync()
        {
            User = HttpSession.Instance.User?.ToObject<User>();
        }


        public static void Set(User user)
        {
            User = user;
        }
    }
}
