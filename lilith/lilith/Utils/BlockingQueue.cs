// 阻塞队列 —— Queue<T> + 一把锁 + 条件变量(C# 用 Monitor.Wait/Pulse)。
using System.Collections.Generic;
using System.Threading;


namespace lilith.Utils
{
    /// <summary>
    /// 为「省电」而生的阻塞队列。消费者没活时 Wait() 睡死, 生产者 Enqueue 时 Pulse
    /// 唤醒, 空闲期 CPU 接近 0 —— 这才是和"自旋轮询"的本质区别(跟用不用
    /// ConcurrentQueue 无关, ConcurrentQueue 也能很省, 关键是别空转)。
    ///
    /// C# 没有独立的 ConditionVariable 类型, Monitor 本身就是「mutex + 条件变量」二合一。
    ///
    /// 典型用法(ioSend 消费者):
    ///   while (running) {
    ///       while (q.TryDequeue(out var x)) Process(x);    // 先把现有的全 drain 掉
    ///       safeKcp.Update(now);
    ///       int wait = (int)(safeKcp.Check(now) - now);    // 下次该醒的时刻
    ///       q.Wait(wait);                                  // 睡到: 有人投递 / Signal / 超时
    ///   }
    /// 生产者(主线程): q.Enqueue(x);
    /// </summary>
    public class BlockingQueue<T>
    {
        readonly Queue<T> queue = new Queue<T>();
        readonly object locker = new object();

        // 入队并唤醒一个等待中的消费者。
        public void Enqueue(T item)
        {
            lock (locker)
            {
                queue.Enqueue(item);
                Monitor.Pulse(locker);
            }
        }

        // 非阻塞取一个; 空则返回 false。消费者循环 drain 用。
        public bool TryDequeue(out T item)
        {
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

        // 队列为空时最多睡 timeoutMs 毫秒; 期间被 Enqueue/Signal 唤醒会提前返回。
        // 空检查紧贴在锁内 → 不会丢唤醒(生产者要拿同一把锁才能 Pulse)。
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

        // 不入队, 仅唤醒消费者。给 recv 线程在 Input 后叫醒 send 线程及时 flush ACK 用。
        public void Signal()
        {
            lock (locker)
            {
                Monitor.Pulse(locker);
            }
        }

        // 清空(断开清理用)。
        public void Clear()
        {
            lock (locker)
            {
                queue.Clear();
            }
        }
    }
}
