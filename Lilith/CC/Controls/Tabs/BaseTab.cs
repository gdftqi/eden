using Avalonia;
using Avalonia.Controls;
using Avalonia.Threading;
using System;
using System.Collections.Generic;

namespace CC
{
    public class BaseTab : UserControl
    {
        private static readonly List<BaseTab> tabs = new();


        protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
        {
            base.OnAttachedToVisualTree(e);

            if (!tabs.Contains(this))
            {
                tabs.Add(this);
            }
        }


        protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
        {
            base.OnDetachedFromVisualTree(e);
            tabs.Remove(this);
        }


        /// <summary>
        /// 收到一条通知
        /// </summary>
        protected virtual void OnNotify(Message msg)
        {
        }


        /// <summary>广播给所有页签(发送者自己除外).</summary>
        public static void Notify(Message msg)
        {
            Post(msg, null);
        }


        /// <summary>
        /// 只发给某一类页签
        /// </summary>
        public static void Notify<T>(Message msg) where T : BaseTab
        {
            Post(msg, typeof(T));
        }


        private static void Post(Message msg, Type? target)
        {
            Dispatcher.UIThread.Post(() => Dispatch(msg, target));
        }


        private static void Dispatch(Message msg, Type? target)
        {
            var snapshot = tabs.ToArray();

            foreach (var tab in snapshot)
            {
                if (ReferenceEquals(tab, msg.Sender))
                {
                    continue;
                }

                if (target != null && !target.IsInstanceOfType(tab))
                {
                    continue;
                }

                tab.OnNotify(msg);
            }
        }
    }
}
