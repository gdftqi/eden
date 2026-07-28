using System.Collections.Generic;
using System.Threading;


namespace Lilith.Utils
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

        public int Wait(T[] items, int timeoutMs = 0)
        {
            lock (locker)
            {
                if (queue.Count == 0 && timeoutMs > 0)
                {
                    Monitor.Wait(locker, timeoutMs);
                }

                int n = 0;
                while (n < items.Length && queue.Count > 0)
                {
                    items[n++] = queue.Dequeue();
                }
                return n;
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
