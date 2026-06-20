// 阻塞队列 —— Queue<T> + 一把锁 + 条件变量(C# 用 Monitor.Wait/Pulse)。
using System.Collections.Generic;
using System.Threading;


namespace lilith.Utils
{
    public class BlockingQueue<T>
    {
        readonly Queue<T> queue = new Queue<T>();
        readonly object locker = new object();

        public void Enqueue(T item)
        {// 入队
            lock (locker)
            {
                queue.Enqueue(item);
                Monitor.Pulse(locker);
            }
        }

        public bool Dequeue(out T item)
        {// 出队
            lock (locker)
            {
                if (queue.Count > 0)
                {
                    item = queue.Dequeue();
                    return true;
                }
                item = default!;
                return false;
            }
        }

        public void Wait(int timeoutMs)
        {
            if (timeoutMs <= 0)
            {
                return;   // 已到 tick 时刻(或已逾期), 不睡, 直接回去跑 Update
            }

            lock (locker)
            {
                if (queue.Count == 0)
                {
                    Monitor.Wait(locker, timeoutMs);
                }
            }
        }

        public void Signal()
        {
            lock (locker)
            {
                Monitor.Pulse(locker);
            }
        }

        public void Clear()
        {// 清空
            lock (locker)
            {
                queue.Clear();
            }
        }
    }
}
